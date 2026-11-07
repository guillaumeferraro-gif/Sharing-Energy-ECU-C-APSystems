#pragma once
// Serveur limite aux routes GET/POST de ce diagnostic. Aucun upload, SSE,
// keep-alive ou chunked. Un navigateur lent ne doit pas monopoliser loop().
#ifndef ACC_HOST_TEST
#include <Arduino.h>
#include <WiFi.h>
#include <lwip/sockets.h>
#include <errno.h>
#endif
#include <cstring>
#include <cstdlib>
#include <cctype>

enum AccHttpMethod { ACC_HTTP_GET, ACC_HTTP_POST };

class CooperativeWebServer {
  static constexpr size_t REQUEST_CAP = 4096, BODY_CAP = 1024, ROUTE_CAP = 20, ARG_CAP = 8;
  struct Route { const char* path=nullptr; AccHttpMethod method=ACC_HTTP_GET; void (*handler)()=nullptr; } routes[ROUTE_CAP];
  struct Arg { String key, value; } args[ARG_CAP];
  WiFiServer server;
  WiFiClient client;
  char request[REQUEST_CAP+1] = {};
  size_t received=0, headerEnd=0, expected=0, routeCount=0, argCount=0;
  String headers, outputHeader, outputBody;
  size_t sentHeader=0, sentBody=0;
  uint32_t started=0;
  bool replying=false, active=false;

  static bool same(const char* a, size_t n, const char* b) {
    if (strlen(b)!=n) return false;
    for (size_t i=0;i<n;++i) if (tolower(static_cast<unsigned char>(a[i]))!=tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
  }
  static int hex(char c) {
    if (c>='0'&&c<='9') return c-'0';
    if (c>='a'&&c<='f') return c-'a'+10;
    if (c>='A'&&c<='F') return c-'A'+10;
    return -1;
  }
  static bool unescape(const char* first, const char* end, String& out) {
    char buffer[257]; size_t used=0;
    for (const char* p=first;p<end;++p) {
      unsigned char c=static_cast<unsigned char>(*p);
      if (c=='+') c=' ';
      else if (c=='%') {
        if (end-p<3 || hex(p[1])<0 || hex(p[2])<0) return false;
        c=static_cast<unsigned char>(hex(p[1])*16+hex(p[2])); p+=2;
      }
      if (c<32 || c==127 || used==sizeof(buffer)-1) return false;
      buffer[used++]=char(c);
    }
    buffer[used]=0; out=buffer;
    return out.length()==used;
  }
  bool parseForm(const char* p, const char* end) {
    while (p<end) {
      const char* next=static_cast<const char*>(memchr(p,'&',size_t(end-p)));
      if (!next) next=end;
      const char* eq=static_cast<const char*>(memchr(p,'=',size_t(next-p)));
      if (!eq || eq==p || eq-p>32 || argCount==ARG_CAP) return false;
      String key,value;
      if (!unescape(p,eq,key)||!unescape(eq+1,next,value)||hasArg(key)) return false;
      args[argCount].key=key; args[argCount].value=value; ++argCount;
      p=next==end ? end : next+1;
    }
    return true;
  }
  void resetClient() {
    client.stop(); active=false; replying=false;
    received=headerEnd=expected=argCount=sentHeader=sentBody=0;
    headers=""; outputHeader=""; outputBody="";
    for (auto& a:args) { a.key=""; a.value=""; }
  }
  bool inspectHeaders() {
    const char* line=strstr(request,"\r\n");
    if (!line) return false;
    line+=2; bool lengthSeen=false, typeSeen=false;
    size_t bodyLength=0;
    while (line<request+headerEnd-2) {
      const char* end=strstr(line,"\r\n");
      if (!end || end==line) break;
      const char* colon=static_cast<const char*>(memchr(line,':',size_t(end-line)));
      if (!colon || colon==line) return false;
      for (const char* p=line;p<colon;++p) if (!(isalnum(static_cast<unsigned char>(*p))||*p=='-')) return false;
      const char* val=colon+1; while (val<end && (*val==' '||*val=='\t')) ++val;
      const char* trimmed=end; while (trimmed>val && (trimmed[-1]==' '||trimmed[-1]=='\t')) --trimmed;
      if (same(line,size_t(colon-line),"Content-Length")) {
        if (lengthSeen||val==trimmed) return false;
        lengthSeen=true;
        for (const char* p=val;p<trimmed;++p) {
          if (*p<'0'||*p>'9') return false;
          bodyLength=bodyLength*10+size_t(*p-'0');
          if (bodyLength>BODY_CAP) return false;
        }
      } else if (same(line,size_t(colon-line),"Content-Type")) {
        if (typeSeen) return false;
        typeSeen=true;
        const char* semi=static_cast<const char*>(memchr(val,';',size_t(trimmed-val)));
        const char* typeEnd=semi ? semi : trimmed;
        if (!same(val,size_t(typeEnd-val),"application/x-www-form-urlencoded")) return false;
      } else if (same(line,size_t(colon-line),"Transfer-Encoding") || same(line,size_t(colon-line),"Expect")) return false;
      line=end+2;
    }
    const bool post=strncmp(request,"POST ",5)==0;
    if ((post&&(!lengthSeen||!typeSeen))||(!post&&bodyLength)) return false;
    expected=headerEnd+bodyLength;
    return expected<=REQUEST_CAP;
  }
  void dispatch() {
    const char* lineEnd=strstr(request,"\r\n");
    const char* space=strchr(request,' ');
    if (!lineEnd||!space||space>=lineEnd) { send(400,"text/plain","Requete invalide"); return; }
    AccHttpMethod method;
    if (space-request==3&&!memcmp(request,"GET",3)) method=ACC_HTTP_GET;
    else if (space-request==4&&!memcmp(request,"POST",4)) method=ACC_HTTP_POST;
    else { send(405,"text/plain","Methode non prise en charge"); return; }
    const char* path=space+1;
    const char* pathEnd=static_cast<const char*>(memchr(path,' ',size_t(lineEnd-path)));
    if (!pathEnd || path==pathEnd || *path!='/' ||
        !((lineEnd-pathEnd==9)&&(!memcmp(pathEnd+1,"HTTP/1.1",8)||!memcmp(pathEnd+1,"HTTP/1.0",8)))) {
      send(400,"text/plain","Requete invalide"); return;
    }
    const char* query=static_cast<const char*>(memchr(path,'?',size_t(pathEnd-path)));
    if (query && !parseForm(query+1,pathEnd)) { send(400,"text/plain","Parametres invalides"); return; }
    if (method==ACC_HTTP_POST && !parseForm(request+headerEnd,request+expected)) { send(400,"text/plain","Formulaire invalide"); return; }
    const size_t pathLength=size_t((query?query:pathEnd)-path);
    ++requests;
    for (size_t i=0;i<routeCount;++i) if (routes[i].method==method && strlen(routes[i].path)==pathLength && !memcmp(path,routes[i].path,pathLength)) {
      routes[i].handler();
      if (!replying) send(500,"text/plain","Reponse absente");
      return;
    }
    send(404,"text/plain","Page inconnue");
  }
public:
  uint32_t requests=0, timeouts=0, ioErrors=0, maxServiceMs=0;
  explicit CooperativeWebServer(uint16_t port):server(port) {}
  void on(const char* path, AccHttpMethod method, void (*handler)()) {
    if (routeCount<ROUTE_CAP) routes[routeCount++]={path,method,handler};
  }
  void begin() { server.begin(); server.setNoDelay(true); }
  void stop() { resetClient(); server.end(); }
  bool hasArg(const String& key) const {
    for (size_t i=0;i<argCount;++i) if (args[i].key==key) return true;
    return false;
  }
  String arg(const String& key) const {
    for (size_t i=0;i<argCount;++i) if (args[i].key==key) return args[i].value;
    return String("");
  }
  void sendHeader(const String& name, const String& value) {
    if (strpbrk(name.c_str(),"\r\n")||strpbrk(value.c_str(),"\r\n")||headers.length()>1024) return;
    headers+=name+": "+value+"\r\n";
  }
  void send(int code, const char* type, const String& body) {
    if (replying) return;
    outputBody=body;
    if (outputBody.length()!=body.length() || body.length()>65536) { outputBody="Memoire insuffisante"; code=503; type="text/plain"; }
    outputHeader="HTTP/1.1 "+String(code)+" Response\r\nConnection: close\r\nContent-Type: "+String(type)+
      "\r\nContent-Length: "+String(outputBody.length())+"\r\n"+headers+"\r\n";
    replying=true; started=millis(); sentHeader=sentBody=0;
  }
  void handleClient() {
    const uint32_t at=millis(); service();
    const uint32_t duration=uint32_t(millis()-at);
    if (duration>maxServiceMs) maxServiceMs=duration;
  }
  void service() {
    if (!active) {
      client=server.accept();
      if (client.fd()<0) return;
      active=true; started=millis();
    }
    if (uint32_t(millis()-started)>=(replying?15000u:5000u)) { ++timeouts; resetClient(); return; }
    if (replying) {
      const bool header=sentHeader<outputHeader.length();
      const String& data=header?outputHeader:outputBody;
      size_t& offset=header?sentHeader:sentBody;
      const size_t remaining=data.length()-offset;
      if (!remaining) { resetClient(); return; }
      const int n=::send(client.fd(),data.c_str()+offset,remaining>1024?1024:remaining,MSG_DONTWAIT);
      if (n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR) { ++ioErrors; resetClient(); return; }
      if (n>0) offset+=size_t(n);
      if (sentHeader==outputHeader.length()&&sentBody==outputBody.length()) resetClient();
      return;
    }
    uint8_t bytes[512];
    const int n=recv(client.fd(),bytes,sizeof(bytes),MSG_DONTWAIT);
    if (!n) { resetClient(); return; }
    if (n<0) {
      if (errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR) { ++ioErrors; resetClient(); }
      return;
    }
    for (int i=0;i<n;++i) {
      const unsigned c=bytes[i];
      if (!c || (c<32&&c!='\r'&&c!='\n'&&c!='\t') || received==REQUEST_CAP) { send(400,"text/plain","Requete invalide ou trop longue"); return; }
      request[received++]=char(c); request[received]=0;
      if (!headerEnd && received>=4&&!memcmp(request+received-4,"\r\n\r\n",4)) {
        headerEnd=received;
        if (!inspectHeaders()) { send(400,"text/plain","Entetes invalides"); return; }
      }
      if (headerEnd && received==expected) { dispatch(); return; }
    }
  }
};
