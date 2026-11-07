/* REA ev_42b0b0eb4d8febfc525e3ae0086aafb79cc957d774578f37c0b1a1ad2060781b */

undefined4 meter_thread_getMeterInfoNew(void)

{
  int iVar1;
  double dVar2;
  char acStack_860 [2048];
  int local_60;
  int local_5c;
  int local_58;
  int local_54;
  int local_50;
  int local_4c;
  int local_48;
  int local_44;
  int local_40;
  int local_3c;
  int local_38;
  int local_34;
  int local_30;
  int local_2c;
  undefined4 local_28;
  int local_24;
  int local_20;
  int local_1c;
  int local_18;
  FILE *local_14;
  int local_10;
  undefined4 local_c;
  
  memset(acStack_860,0,0x800);
  local_10 = 0;
  local_c = 0;
  local_14 = fopen("/etc/yuneng/meterFuncNewInfo.conf","r");
  if (local_14 != (FILE *)0x0) {
    fgets(acStack_860,0x800,local_14);
    local_10 = cJSON_Parse(acStack_860);
    if (local_10 != 0) {
      local_18 = cJSON_GetObjectItem(local_10,&DAT_000e6470);
      if ((local_18 != 0) && (*(int *)(local_18 + 0xc) == 0x10)) {
        g_meter_mode = atoi(*(char **)(local_18 + 0x10));
      }
      if (g_meter_mode == 1) {
        systemInfoR._64_4_ = 0;
        local_1c = cJSON_GetObjectItem(local_10,"zeroExport");
        if (local_1c != 0) {
          local_20 = cJSON_GetObjectItem(local_1c,&DAT_000e64d8);
          local_24 = cJSON_GetObjectItem(local_1c,"singleSet");
          local_28 = cJSON_GetObjectItem(local_1c,"relay");
          local_2c = cJSON_GetObjectItem(local_1c,&DAT_000e64f4);
          local_30 = cJSON_GetObjectItem(local_1c,&DAT_000e64f8);
          local_34 = cJSON_GetObjectItem(local_1c,&DAT_000e64fc);
          local_38 = cJSON_GetObjectItem(local_1c,&DAT_000e6500);
          if (local_20 == 0) {
            systemInfoR._60_4_ = 0;
          }
          else if (*(int *)(local_20 + 0xc) == 0x10) {
            systemInfoR._60_4_ = atoi(*(char **)(local_20 + 0x10));
          }
          if ((local_24 != 0) && (*(int *)(local_24 + 0xc) == 0x10)) {
            systemInfoR._64_4_ = atoi(*(char **)(local_24 + 0x10));
          }
          if (((local_2c == 0) || (*(int *)(local_2c + 0xc) != 0x10)) ||
             ((systemInfoR._60_4_ != 1 && (systemInfoR._60_4_ != 0)))) {
            if (((local_30 == 0) || (*(int *)(local_30 + 0xc) != 0x10)) || (systemInfoR._60_4_ != 2)
               ) {
              if (((local_34 == 0) || (*(int *)(local_34 + 0xc) != 0x10)) ||
                 (systemInfoR._60_4_ != 3)) {
                if (((local_38 != 0) && (*(int *)(local_38 + 0xc) == 0x10)) &&
                   (systemInfoR._60_4_ == 4)) {
                  systemInfoR._48_4_ = atoi(*(char **)(local_38 + 0x10));
                }
              }
              else {
                systemInfoR._48_4_ = atoi(*(char **)(local_34 + 0x10));
              }
            }
            else {
              systemInfoR._48_4_ = atoi(*(char **)(local_30 + 0x10));
            }
          }
          else {
            systemInfoR._48_4_ = atoi(*(char **)(local_2c + 0x10));
          }
        }
      }
      else if (g_meter_mode == 2) {
        local_3c = cJSON_GetObjectItem(local_10,"relayControl");
        if (local_3c != 0) {
          local_40 = cJSON_GetObjectItem(local_3c,&DAT_000e6514);
          local_44 = cJSON_GetObjectItem(local_3c,&DAT_000e651c);
          if (((local_40 != 0) && (*(int *)(local_40 + 0xc) == 0x10)) &&
             ((local_44 != 0 && (*(int *)(local_44 + 0xc) == 0x10)))) {
            dVar2 = atof(*(char **)(local_40 + 0x10));
            systemInfoR._52_4_ = (undefined4)(longlong)dVar2;
            dVar2 = atof(*(char **)(local_44 + 0x10));
            systemInfoR._56_4_ = (undefined4)(longlong)dVar2;
          }
        }
      }
      else if (g_meter_mode == 3) {
        local_48 = cJSON_GetObjectItem(local_10,"threeBalance");
        if (((local_48 != 0) &&
            (local_4c = cJSON_GetObjectItem(local_48,&DAT_000e6534), local_4c != 0)) &&
           (*(int *)(local_4c + 0xc) == 0x10)) {
          withoutCTflag = atoi(*(char **)(local_4c + 0x10));
        }
      }
      else {
        local_50 = cJSON_GetObjectItem(local_10,"fastThree");
        if (((local_50 != 0) &&
            (local_54 = cJSON_GetObjectItem(local_50,&DAT_000e6534), local_54 != 0)) &&
           (*(int *)(local_54 + 0xc) == 0x10)) {
          withoutCTflag = atoi(*(char **)(local_54 + 0x10));
        }
      }
      local_58 = cJSON_GetObjectItem(local_10,&DAT_000e6484);
      local_5c = cJSON_GetObjectItem(local_10,&DAT_000e6488);
      local_60 = cJSON_GetObjectItem(local_10,&DAT_000e648c);
      if (((((g_meter_mode == 2) && (local_58 != 0)) && (local_5c != 0)) &&
          ((local_60 != 0 && (**(char **)(local_58 + 0x10) != '\0')))) &&
         ((**(char **)(local_5c + 0x10) != '\0' && (**(char **)(local_60 + 0x10) != '\0')))) {
        g_meter_TPHFL = 1;
        g_doccInfo[10] = 2;
        get_phase_R(*(undefined4 *)(local_58 + 0x10),*(undefined4 *)(local_5c + 0x10),
                    *(undefined4 *)(local_60 + 0x10));
      }
      cJSON_Delete(local_10);
      local_c = 1;
    }
    fclose(local_14);
  }
  iVar1 = get_ints_from_file("/etc/yuneng/cc_dan.conf");
  if (iVar1 == 1) {
    systemInfoR._64_4_ = 1;
  }
  return local_c;
}



