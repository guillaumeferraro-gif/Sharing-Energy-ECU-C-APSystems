/* REA ev_509afed8837947ff50bc274f162efbdda7847c0f1e74792dc5ec82992ae230be */

void saveMeterData(void)

{
  undefined4 uVar1;
  size_t __size;
  char acStack_80 [100];
  char *local_1c;
  FILE *local_18;
  undefined4 local_14;
  undefined4 local_10;
  undefined4 local_c;
  
  local_c = cJSON_CreateObject();
  if (g_meter[2] == '\x01') {
    local_10 = cJSON_CreateObject();
    if ((float)g_meter._160_4_ <= 3.4028235e+38) {
      uVar1 = int_to_str((int)(float)g_meter._160_4_);
      cJSON_AddStringToObject(local_10,&DAT_000e5aec,uVar1);
      memset(acStack_80,0,100);
      sprintf(acStack_80,"echo %d >/tmp/gridQuickPower.conf",(int)(float)g_meter._160_4_);
      system(acStack_80);
    }
    if ((float)g_meter._148_4_ <= 3.4028235e+38) {
      uVar1 = int_to_str((int)(float)g_meter._148_4_);
      cJSON_AddStringToObject(local_10,&DAT_000e5b14,uVar1);
    }
    if ((float)g_meter._152_4_ <= 3.4028235e+38) {
      uVar1 = int_to_str((int)(float)g_meter._152_4_);
      cJSON_AddStringToObject(local_10,&DAT_000e5b18,uVar1);
    }
    if ((float)g_meter._156_4_ <= 3.4028235e+38) {
      uVar1 = int_to_str((int)(float)g_meter._156_4_);
      cJSON_AddStringToObject(local_10,&DAT_000e5b1c,uVar1);
    }
    cJSON_AddItemToObject(local_c,"meterGrid",local_10);
  }
  if (g_meter[0x25a] == '\x01') {
    local_14 = cJSON_CreateObject();
    if ((float)g_meter._760_4_ <= 3.4028235e+38) {
      uVar1 = int_to_str((int)(float)g_meter._760_4_);
      cJSON_AddStringToObject(local_14,&DAT_000e5aec,uVar1);
      memset(acStack_80,0,100);
      sprintf(acStack_80,"echo %d >/tmp/pvQuickPower.conf",(int)(float)g_meter._760_4_);
      system(acStack_80);
    }
    if ((float)g_meter._748_4_ <= 3.4028235e+38) {
      uVar1 = int_to_str((int)(float)g_meter._748_4_);
      cJSON_AddStringToObject(local_14,&DAT_000e5b14,uVar1);
    }
    if ((float)g_meter._752_4_ <= 3.4028235e+38) {
      uVar1 = int_to_str((int)(float)g_meter._752_4_);
      cJSON_AddStringToObject(local_14,&DAT_000e5b18,uVar1);
    }
    if ((float)g_meter._756_4_ <= 3.4028235e+38) {
      uVar1 = int_to_str((int)(float)g_meter._756_4_);
      cJSON_AddStringToObject(local_14,&DAT_000e5b1c,uVar1);
    }
    cJSON_AddItemToObject(local_c,"meterPV",local_14);
  }
  local_18 = fopen("/tmp/meterDataTemp.conf","w");
  if (local_18 != (FILE *)0x0) {
    local_1c = (char *)cJSON_PrintUnformatted(local_c);
    __size = strlen(local_1c);
    fwrite(local_1c,__size,1,local_18);
    fclose(local_18);
    cJSON_free(local_1c);
  }
  cJSON_Delete(local_c);
  return;
}



