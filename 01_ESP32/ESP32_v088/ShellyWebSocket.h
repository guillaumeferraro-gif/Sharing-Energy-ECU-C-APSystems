#pragma once
// Client WS local borne : aucun thread, DNS, TLS, compression ou attente
// bloquante. Un seul socket, 8192 octets par message, deux petits envois.
#ifndef ACC_HOST_TEST
#include <Arduino.h>
#include <WiFi.h>
#include <lwip/sockets.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <esp_random.h>
#include <mbedtls/sha1.h>
#include <mbedtls/base64.h>
#endif
#include <cstring>
#include <cstdio>
#include <cctype>

class ShellyWebSocket {
public:
  enum Phase { IDLE, CONNECTING, HANDSHAKE, OPEN, CLOSING, FAILED };
  using MessageHandler = void (*)(const char*, size_t);
  static constexpr size_t MESSAGE_CAP = 8192;
  Phase phase=IDLE;
  const char* error="aucune";
  int socketError=0, httpCode=0;
  uint32_t connectDurationMs=0, framesReceived=0, messagesReceived=0, pingsReceived=0;
private:
  int socketFd=-1;
  uint32_t phaseAt=0;
  MessageHandler handler=nullptr;
  char upgradeRequest[384]={}, upgradeHeader[1537]={}, expectedAccept[29]={};
  size_t upgradeLength=0, upgradeSent=0, headerUsed=0;
  struct Outgoing { uint8_t data[264]={}; size_t size=0, offset=0; uint32_t at=0; } outgoing[2];
  unsigned txHead=0, txCount=0;
  uint8_t frameHeader[4]={}, control[125]={};
  size_t frameHeaderUsed=0, frameHeaderNeed=2, frameLength=0, frameAt=0;
  uint8_t opcode=0;
  bool finalFrame=false, fragmented=false;
  char message[MESSAGE_CAP+1]={};
  size_t messageUsed=0;

  static bool same(const char* a, size_t n, const char* b) {
    if (strlen(b)!=n) return false;
    for (size_t i=0;i<n;++i) if (tolower(static_cast<unsigned char>(a[i]))!=tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
  }
  static bool token(const char* a, const char* end, const char* wanted) {
    while (a<end) {
      while (a<end&&(*a==' '||*a=='\t'||*a==',')) ++a;
      const char* b=a; while (b<end&&*b!=',') ++b;
      const char* trim=b; while (trim>a&&(trim[-1]==' '||trim[-1]=='\t')) --trim;
      if (same(a,size_t(trim-a),wanted)) return true;
      a=b==end?end:b+1;
    }
    return false;
  }
  static bool utf8(const uint8_t* data, size_t size) {
    uint32_t code=0, minimum=0; unsigned pending=0;
    for (size_t i=0;i<size;++i) {
      const uint8_t c=data[i];
      if (!pending) {
        if (c<128) continue;
        if (c>=0xc2&&c<=0xdf) { code=c&31; minimum=0x80; pending=1; }
        else if (c>=0xe0&&c<=0xef) { code=c&15; minimum=0x800; pending=2; }
        else if (c>=0xf0&&c<=0xf4) { code=c&7; minimum=0x10000; pending=3; }
        else return false;
      } else {
        if ((c&0xc0)!=0x80) return false;
        code=(code<<6)|(c&63);
        if (!--pending && (code<minimum||code>0x10ffff||(code>=0xd800&&code<=0xdfff))) return false;
      }
    }
    return !pending;
  }
  void fail(const char* reason, int number=0) {
    if (socketFd>=0) { close(socketFd); socketFd=-1; }
    phase=FAILED; error=reason; socketError=number; txCount=0;
  }
  void connected() { connectDurationMs=uint32_t(millis()-phaseAt); phase=HANDSHAKE; phaseAt=millis(); }
  bool checkUpgrade() {
    if (headerUsed<14 || memcmp(upgradeHeader,"HTTP/1.1 ",9)) return false;
    for (unsigned i=9;i<12;++i) if (upgradeHeader[i]<'0'||upgradeHeader[i]>'9') return false;
    if (upgradeHeader[12]!=' '&&upgradeHeader[12]!='\r') return false;
    httpCode=(upgradeHeader[9]-'0')*100+(upgradeHeader[10]-'0')*10+upgradeHeader[11]-'0';
    if (httpCode!=101) return false;
    const char* p=strstr(upgradeHeader,"\r\n"); if (!p) return false; p+=2;
    bool upgrade=false, connection=false, accept=false;
    while (*p && strncmp(p,"\r\n",2)) {
      const char* end=strstr(p,"\r\n"); if (!end) return false;
      const char* colon=static_cast<const char*>(memchr(p,':',size_t(end-p))); if (!colon||colon==p) return false;
      const char* value=colon+1; while (value<end&&(*value==' '||*value=='\t')) ++value;
      const char* trim=end; while (trim>value&&(trim[-1]==' '||trim[-1]=='\t')) --trim;
      if (same(p,size_t(colon-p),"Upgrade")) {
        if (upgrade||!same(value,size_t(trim-value),"websocket")) return false;
        upgrade=true;
      } else if (same(p,size_t(colon-p),"Connection")) {
        if (!token(value,trim,"Upgrade")) return false;
        connection=true;
      } else if (same(p,size_t(colon-p),"Sec-WebSocket-Accept")) {
        if (accept||size_t(trim-value)!=28||memcmp(value,expectedAccept,28)) return false;
        accept=true;
      } else if (same(p,size_t(colon-p),"Sec-WebSocket-Extensions") || same(p,size_t(colon-p),"Sec-WebSocket-Protocol")) return false;
      p=end+2;
    }
    return upgrade&&connection&&accept;
  }
  bool enqueue(uint8_t type, const uint8_t* data, size_t n) {
    if (txCount==2 || n>256 || (type>=8&&n>125)) return false;
    Outgoing& out=outgoing[(txHead+txCount)%2];
    size_t h=0; out.data[h++]=uint8_t(0x80|type);
    if (n<126) out.data[h++]=uint8_t(0x80|n);
    else { out.data[h++]=0xfe; out.data[h++]=uint8_t(n>>8); out.data[h++]=uint8_t(n); }
    uint8_t mask[4]; esp_fill_random(mask,sizeof(mask));
    memcpy(out.data+h,mask,4); h+=4;
    for (size_t i=0;i<n;++i) out.data[h+i]=uint8_t(data[i]^mask[i%4]);
    out.size=h+n; out.offset=0; out.at=millis(); ++txCount;
    return true;
  }
  void transmit() {
    if (!txCount) return;
    Outgoing& out=outgoing[txHead];
    if (uint32_t(millis()-out.at)>=5000) { fail("ws_envoi_delai",ETIMEDOUT); return; }
    const int n=send(socketFd,out.data+out.offset,out.size-out.offset,MSG_DONTWAIT);
    if (n<0) {
      if (errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR) fail("ws_envoi_impossible",errno);
      return;
    }
    out.offset+=size_t(n);
    if (out.offset==out.size) { txHead=(txHead+1)%2; --txCount; }
  }
  bool prepareFrame() {
    finalFrame=(frameHeader[0]&0x80)!=0; opcode=frameHeader[0]&15;
    if ((frameHeader[0]&0x70)||(frameHeader[1]&0x80)) { fail("ws_trame_invalide"); return false; }
    frameLength=frameHeader[1]&127;
    if (frameLength==127) { fail("ws_message_trop_grand"); return false; }
    if (frameLength==126) {
      frameLength=size_t(frameHeader[2])*256+frameHeader[3];
      if (frameLength<126) { fail("ws_longueur_invalide"); return false; }
    }
    if (opcode>=8) {
      if (!finalFrame||frameLength>125||(opcode!=8&&opcode!=9&&opcode!=10)||(opcode==8&&frameLength==1)) {
        fail("ws_controle_invalide"); return false;
      }
    } else {
      if ((opcode!=0&&opcode!=1)||(opcode==0&&!fragmented)||(opcode==1&&fragmented)) {
        fail("ws_fragment_invalide"); return false;
      }
      if (opcode==1) messageUsed=0;
      if (messageUsed+frameLength>MESSAGE_CAP) { fail("ws_message_trop_grand"); return false; }
    }
    frameAt=0; return true;
  }
  void finishFrame() {
    ++framesReceived;
    if (opcode==9) {
      ++pingsReceived;
      if (!enqueue(10,control,frameLength)) fail("ws_file_envoi_pleine");
    } else if (opcode==8) {
      if (frameLength>=2) {
        const unsigned code=unsigned(control[0])*256+control[1];
        if (!((code>=1000&&code<=1014&&code!=1004&&code!=1005&&code!=1006)||(code>=3000&&code<=4999)) ||
            !utf8(control+2,frameLength-2)) { fail("ws_fermeture_invalide"); return; }
      }
      if (!enqueue(8,control,frameLength)) { fail("ws_file_envoi_pleine"); return; }
      phase=CLOSING; phaseAt=millis();
    } else if (opcode==0||opcode==1) {
      fragmented=!finalFrame;
      if (finalFrame) {
        if (!utf8(reinterpret_cast<const uint8_t*>(message),messageUsed)) { fail("ws_utf8_invalide"); return; }
        message[messageUsed]=0; ++messagesReceived;
        if (handler) handler(message,messageUsed);
        messageUsed=0;
      }
    }
    frameHeaderUsed=0; frameHeaderNeed=2;
  }
  void frameByte(uint8_t byte) {
    if (frameHeaderUsed<frameHeaderNeed) {
      frameHeader[frameHeaderUsed++]=byte;
      if (frameHeaderUsed==2 && (frameHeader[1]&127)==126) frameHeaderNeed=4;
      if (frameHeaderUsed<frameHeaderNeed) return;
      if (!prepareFrame()) return;
      if (!frameLength) finishFrame();
      return;
    }
    if (opcode>=8) control[frameAt]=byte;
    else message[messageUsed++]=char(byte);
    if (++frameAt==frameLength) finishFrame();
  }
public:
  ~ShellyWebSocket() { stop(); }
  void stop() {
    if (socketFd>=0) { close(socketFd); socketFd=-1; }
    phase=IDLE; txCount=0;
  }
  void begin(const IPAddress& address, MessageHandler callback) {
    stop(); handler=callback; error="aucune"; socketError=httpCode=0; phaseAt=millis();
    headerUsed=upgradeSent=0; txHead=txCount=0;
    frameHeaderUsed=0; frameHeaderNeed=2; messageUsed=0; fragmented=false;
    uint8_t nonce[16], digest[20]; char key[25]={}, joined[61]={}; size_t length=0;
    esp_fill_random(nonce,sizeof(nonce));
    if (mbedtls_base64_encode(reinterpret_cast<unsigned char*>(key),sizeof(key),&length,nonce,sizeof(nonce)) || length!=24) { fail("ws_cle_impossible"); return; }
    snprintf(joined,sizeof(joined),"%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11",key);
    if (mbedtls_sha1(reinterpret_cast<const unsigned char*>(joined),strlen(joined),digest) ||
        mbedtls_base64_encode(reinterpret_cast<unsigned char*>(expectedAccept),sizeof(expectedAccept),&length,digest,sizeof(digest)) || length!=28) {
      fail("ws_cle_impossible"); return;
    }
    const int size=snprintf(upgradeRequest,sizeof(upgradeRequest),
      "GET /rpc HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n",address.toString().c_str(),key);
    if (size<0||size_t(size)>=sizeof(upgradeRequest)) { fail("ws_requete_trop_grande"); return; }
    upgradeLength=size_t(size);
    socketFd=socket(AF_INET,SOCK_STREAM,0);
    if (socketFd<0) { fail("connexion_impossible",errno); return; }
    const int flags=fcntl(socketFd,F_GETFL,0);
    if (flags<0 || fcntl(socketFd,F_SETFL,flags|O_NONBLOCK)<0) { fail("connexion_impossible",errno); return; }
    sockaddr_in target={}; target.sin_family=AF_INET; target.sin_port=htons(80); target.sin_addr.s_addr=address;
    phase=CONNECTING;
    const int result=lwip_connect(socketFd,reinterpret_cast<const sockaddr*>(&target),sizeof(target));
    if (!result) connected();
    else if (errno!=EINPROGRESS&&errno!=EWOULDBLOCK&&errno!=EALREADY) fail("connexion_impossible",errno);
  }
  bool sendText(const char* data, size_t size) {
    return phase==OPEN && enqueue(1,reinterpret_cast<const uint8_t*>(data),size);
  }
  void service() {
    if (phase==IDLE||phase==FAILED) return;
    if (phase==CONNECTING) {
      if (uint32_t(millis()-phaseAt)>=5000) { fail("delai_connexion_depasse",ETIMEDOUT); return; }
      fd_set writes,errors; FD_ZERO(&writes); FD_ZERO(&errors); FD_SET(socketFd,&writes); FD_SET(socketFd,&errors);
      timeval noWait={0,0};
      const int n=select(socketFd+1,nullptr,&writes,&errors,&noWait);
      if (n<0) { if (errno!=EINTR) fail("connexion_impossible",errno); return; }
      if (!n) return;
      int err=0; socklen_t size=sizeof(err);
      if (getsockopt(socketFd,SOL_SOCKET,SO_ERROR,&err,&size)<0) { fail("connexion_impossible",errno); return; }
      if (err) { fail("connexion_impossible",err); return; }
      connected(); return;
    }
    if (phase==CLOSING) {
      if (uint32_t(millis()-phaseAt)>=1000) { fail("ws_ferme"); return; }
      transmit(); if (phase==CLOSING&&!txCount) fail("ws_ferme"); return;
    }
    if (phase==HANDSHAKE) {
      if (uint32_t(millis()-phaseAt)>=5000) { fail("ws_ouverture_delai",ETIMEDOUT); return; }
      if (upgradeSent<upgradeLength) {
        const int n=send(socketFd,upgradeRequest+upgradeSent,upgradeLength-upgradeSent,MSG_DONTWAIT);
        if (n<0) { if (errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR) fail("ws_envoi_impossible",errno); }
        else upgradeSent+=size_t(n);
        return;
      }
    } else {
      transmit(); if (phase!=OPEN) return;
    }
    uint8_t bytes[512]; const int n=recv(socketFd,bytes,sizeof(bytes),MSG_DONTWAIT);
    if (!n) { fail("connexion_fermee"); return; }
    if (n<0) { if (errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR) fail("ws_reception_impossible",errno); return; }
    for (int i=0;i<n;++i) {
      if (phase==HANDSHAKE) {
        if (!bytes[i]||headerUsed==sizeof(upgradeHeader)-1) { fail("ws_entete_invalide"); return; }
        upgradeHeader[headerUsed++]=char(bytes[i]); upgradeHeader[headerUsed]=0;
        if (headerUsed>=4&&!memcmp(upgradeHeader+headerUsed-4,"\r\n\r\n",4)) {
          if (!checkUpgrade()) { fail(httpCode==401||httpCode==403?"auth_requise":"ws_ouverture_refusee"); return; }
          phase=OPEN; phaseAt=millis();
        }
      } else if (phase==OPEN) frameByte(bytes[i]);
      if (phase!=HANDSHAKE&&phase!=OPEN) return;
    }
  }
};
