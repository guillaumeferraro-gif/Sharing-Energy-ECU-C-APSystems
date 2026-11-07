/* REA ev_932b9f49387540c99c2a52aa2ec20975f150ecb7be56da2cb2092c6e60dc8618 */

/* WARNING: Heritage AFTER dead removal. Example location: r0x00270c8e : 0x000b4784 */
/* WARNING: Restarted to delay deadcode elimination for space: ram */

void meter_get_data_entry(int param_1)

{
  int iVar1;
  undefined4 extraout_r1;
  undefined4 extraout_r1_00;
  uint uVar2;
  undefined1 auStack_330 [3];
  undefined1 auStack_32d [253];
  undefined1 auStack_230 [256];
  undefined1 auStack_130 [8];
  undefined1 auStack_128 [119];
  undefined1 auStack_b1 [129];
  undefined4 local_30;
  int local_2c;
  undefined4 local_28;
  int local_24;
  int local_20;
  int local_1c;
  int local_18;
  byte local_11;
  
  local_11 = 0;
  memset(auStack_130,0,0x100);
  memset(auStack_230,0,0x100);
  if (((g_meter._24_4_ == 0x2202) || (g_meter._624_4_ == 0x2202)) || (g_meter._624_4_ == 0x22e)) {
    if (param_1 == 1) {
      iVar1 = meter_get_data(g_meter[1],0x2806,0x2c,auStack_130,3);
      if (((iVar1 == 0) && (iVar1 = meter_get_data(g_meter[1],0x2044,2,auStack_330,3), iVar1 == 0))
         && (iVar1 = meter_get_data(g_meter[1],0x181e,0x12,auStack_230,3), iVar1 == 0)) {
        memcpy(auStack_b1,auStack_32d,4);
        meter_resolve_DTSU666_data1(auStack_130,0x270d14,local_11);
        printf("g_meter[METER_POS_GRID - 1].stMeter_cur.fActivePower[0]2=%f\n",extraout_r1,
               SUB84((double)(float)g_meter._148_4_,0),
               (int)((ulonglong)(double)(float)g_meter._148_4_ >> 0x20));
        meter_resolve_DTSU666_data2(auStack_230,0x270d14,local_11);
        g_meter._20_4_ = g_meter._20_4_ + 1;
        g_meter[2] = '\x01';
        meterDispalyCountR = meterDispalyCountR + 1;
        system("echo 1 >/tmp/gridStatus.conf");
      }
      else {
        system("echo 0 >/tmp/gridStatus.conf");
        g_meter[2] = '\0';
      }
      iVar1 = meter_get_data(g_meter[0x259],0x2006,0x2c,auStack_130,3);
      if (((iVar1 == 0) &&
          (iVar1 = meter_get_data(g_meter[0x259],0x2044,2,auStack_330,3), iVar1 == 0)) &&
         (iVar1 = meter_get_data(g_meter[0x259],0x101e,0x12,auStack_230,3), iVar1 == 0)) {
        memcpy(auStack_b1,auStack_32d,4);
        meter_resolve_DTSU666_data1(auStack_130,0x270f6c,local_11);
        meter_resolve_DTSU666_data2(auStack_230,0x270f6c,local_11);
        g_meter._620_4_ = g_meter._620_4_ + 1;
        g_meter[0x25a] = '\x01';
        meterDispalyCountR = meterDispalyCountR + 1;
        system("echo 1 >/tmp/pvStatus.conf");
      }
      else {
        system("echo 0 >/tmp/pvStatus.conf");
        g_meter[0x25a] = '\0';
      }
      printf("g_meter[METER_POS_GRID - 1].stMeter_cur.fActivePower[0]3=%f\n");
    }
    else {
      iVar1 = meter_get_data(g_meter[1],0x2806,0x2c,auStack_130,3);
      if ((iVar1 == 0) && (iVar1 = meter_get_data(g_meter[1],0x2044,2,auStack_330,3), iVar1 == 0)) {
        memcpy(auStack_b1,auStack_32d,4);
        meter_resolve_DTSU666_data1(auStack_130,0x270d14,local_11);
        g_meter._20_4_ = g_meter._20_4_ + 1;
        g_meter[2] = '\x01';
        meterDispalyCountR = meterDispalyCountR + 1;
        system("echo 1 >/tmp/gridStatus.conf");
      }
      else {
        system("echo 0 >/tmp/gridStatus.conf");
        g_meter[2] = '\0';
      }
      iVar1 = meter_get_data(g_meter[0x259],0x2006,0x2c,auStack_130,3);
      if ((iVar1 == 0) &&
         (iVar1 = meter_get_data(g_meter[0x259],0x2044,2,auStack_330,3), iVar1 == 0)) {
        memcpy(auStack_b1,auStack_32d,4);
        meter_resolve_DTSU666_data1(auStack_130,0x270f6c,local_11);
        g_meter._620_4_ = g_meter._620_4_ + 1;
        g_meter[0x25a] = '\x01';
        meterDispalyCountR = meterDispalyCountR + 1;
        system("echo 1 >/tmp/pvStatus.conf");
      }
      else {
        system("echo 0 >/tmp/pvStatus.conf");
        g_meter[0x25a] = '\0';
      }
    }
  }
  else if (((g_meter._24_4_ == 0x327) || (g_meter._624_4_ == 0x328)) || (g_meter._624_4_ == 0x32e))
  {
    if (param_1 == 1) {
      iVar1 = meter_get_data(g_meter[1],0x2806,0x2c,auStack_130,3);
      if (((iVar1 == 0) && (iVar1 = meter_get_data(g_meter[1],0x2044,2,auStack_330,3), iVar1 == 0))
         && (iVar1 = meter_get_data(g_meter[1],0x181e,0x12,auStack_230,3), iVar1 == 0)) {
        memcpy(auStack_b1,auStack_32d,4);
        meter_resolve_DTSU666_data1(auStack_130,0x270d14,local_11);
        printf("g_meter[METER_POS_GRID - 1].stMeter_cur.fActivePower[0]2=%f\n",extraout_r1_00,
               SUB84((double)(float)g_meter._148_4_,0),
               (int)((ulonglong)(double)(float)g_meter._148_4_ >> 0x20));
        meter_resolve_DTSU666_data2(auStack_230,0x270d14,local_11);
        g_meter._20_4_ = g_meter._20_4_ + 1;
        g_meter[2] = '\x01';
        meterDispalyCountR = meterDispalyCountR + 1;
        system("echo 1 >/tmp/gridStatus.conf");
      }
      else {
        system("echo 0 >/tmp/gridStatus.conf");
        g_meter[2] = '\0';
      }
      iVar1 = meter_get_data(g_meter[0x259],0x2006,0x2c,auStack_130,3);
      if (((iVar1 == 0) &&
          (iVar1 = meter_get_data(g_meter[0x259],0x2044,2,auStack_330,3), iVar1 == 0)) &&
         (iVar1 = meter_get_data(g_meter[0x259],0x101e,0x12,auStack_230,3), iVar1 == 0)) {
        memcpy(auStack_b1,auStack_32d,4);
        meter_resolve_DTSU666_data1(auStack_130,0x270f6c,local_11);
        meter_resolve_DTSU666_data2(auStack_230,0x270f6c,local_11);
        g_meter._620_4_ = g_meter._620_4_ + 1;
        g_meter[0x25a] = '\x01';
        meterDispalyCountR = meterDispalyCountR + 1;
        system("echo 1 >/tmp/pvStatus.conf");
      }
      else {
        system("echo 0 >/tmp/pvStatus.conf");
        g_meter[0x25a] = '\0';
      }
      printf("g_meter[METER_POS_GRID - 1].stMeter_cur.fActivePower[0]3=%f\n");
    }
    else {
      iVar1 = meter_get_data(g_meter[1],0x2806,0x2c,auStack_130,3);
      if ((iVar1 == 0) && (iVar1 = meter_get_data(g_meter[1],0x2044,2,auStack_330,3), iVar1 == 0)) {
        memcpy(auStack_b1,auStack_32d,4);
        meter_resolve_DTSU666_data1(auStack_130,0x270d14,local_11);
        g_meter._20_4_ = g_meter._20_4_ + 1;
        g_meter[2] = '\x01';
        meterDispalyCountR = meterDispalyCountR + 1;
        system("echo 1 >/tmp/gridStatus.conf");
      }
      else {
        system("echo 0 >/tmp/gridStatus.conf");
        g_meter[2] = '\0';
      }
      iVar1 = meter_get_data(g_meter[0x259],0x2006,0x2c,auStack_130,3);
      if ((iVar1 == 0) &&
         (iVar1 = meter_get_data(g_meter[0x259],0x2044,2,auStack_330,3), iVar1 == 0)) {
        memcpy(auStack_b1,auStack_32d,4);
        meter_resolve_DTSU666_data1(auStack_130,0x270f6c,local_11);
        g_meter._620_4_ = g_meter._620_4_ + 1;
        g_meter[0x25a] = '\x01';
        meterDispalyCountR = meterDispalyCountR + 1;
        system("echo 1 >/tmp/pvStatus.conf");
      }
      else {
        system("echo 0 >/tmp/pvStatus.conf");
        g_meter[0x25a] = '\0';
      }
    }
  }
  else if ((g_meter._24_4_ == 0x2111) || (g_meter._624_4_ == 0x2111)) {
    if (param_1 == 1) {
      if (g_meter._592_4_ == 2) {
        local_2c = 3000;
      }
      else {
        local_2c = 0;
      }
      iVar1 = meter_get_data(g_meter[1],local_2c,0x24,auStack_130,4);
      if ((iVar1 == 0) &&
         (iVar1 = meter_get_data(g_meter[1],local_2c + 0x46,6,auStack_230,4), iVar1 == 0)) {
        meter_resolve_SEM1_data1(auStack_130,0x270d14,0);
        meter_resolve_SEM1_data2(auStack_230,0x270d14,0);
        g_meter._20_4_ = g_meter._20_4_ + 1;
        g_meter[2] = '\x01';
        meterDispalyCountR = meterDispalyCountR + 1;
        system("echo 1 >/tmp/gridStatus.conf");
      }
      else {
        system("echo 0 >/tmp/gridStatus.conf");
        g_meter[2] = '\0';
      }
      if (g_meter._1192_4_ == 2) {
        local_2c = 3000;
      }
      else {
        local_2c = 0;
      }
      iVar1 = meter_get_data(g_meter[0x259],local_2c,0x24,auStack_130,4);
      if ((iVar1 == 0) &&
         (iVar1 = meter_get_data(g_meter[0x259],local_2c + 0x46,6,auStack_230,4), iVar1 == 0)) {
        meter_resolve_SEM1_data1(auStack_130,0x270f6c,1);
        meter_resolve_SEM1_data2(auStack_230,0x270f6c,1);
        g_meter._620_4_ = g_meter._620_4_ + 1;
        g_meter[0x25a] = '\x01';
        meterDispalyCountR = meterDispalyCountR + 1;
        system("echo 1 >/tmp/pvStatus.conf");
      }
      else {
        system("echo 0 >/tmp/pvStatus.conf");
        g_meter[0x25a] = '\0';
      }
    }
    else {
      if (g_meter._592_4_ == 2) {
        local_30 = 3000;
      }
      else {
        local_30 = 0;
      }
      iVar1 = meter_get_data(g_meter[1],local_30,0x24,auStack_130,4);
      if (iVar1 == 0) {
        meter_resolve_SEM1_data1(auStack_130,0x270d14,0);
        g_meter._20_4_ = g_meter._20_4_ + 1;
        g_meter[2] = '\x01';
        meterDispalyCountR = meterDispalyCountR + 1;
        system("echo 1 >/tmp/gridStatus.conf");
      }
      else {
        system("echo 0 >/tmp/gridStatus.conf");
        g_meter[2] = '\0';
      }
      if (g_meter._1192_4_ == 2) {
        local_30 = 3000;
      }
      else {
        local_30 = 0;
      }
      iVar1 = meter_get_data(g_meter[0x259],local_30,0x24,auStack_130,4);
      if (iVar1 == 0) {
        meter_resolve_SEM1_data1(auStack_130,0x270f6c,1);
        g_meter._620_4_ = g_meter._620_4_ + 1;
        g_meter[0x25a] = '\x01';
        meterDispalyCountR = meterDispalyCountR + 1;
        system("echo 1 >/tmp/pvStatus.conf");
      }
      else {
        system("echo 0 >/tmp/pvStatus.conf");
        g_meter[0x25a] = '\0';
      }
    }
  }
  else if ((g_meter._24_4_ == 0x2311) || (g_meter._624_4_ == 0x2311)) {
    if (param_1 == 1) {
      if (g_meter._592_4_ == 2) {
        local_24 = 3000;
      }
      else {
        local_24 = 0;
      }
      iVar1 = meter_get_data(g_meter[1],local_24,0x48,auStack_130,4);
      if ((iVar1 == 0) &&
         (iVar1 = meter_get_data(g_meter[1],local_24 + 0x15a,0xc,auStack_230,4), iVar1 == 0)) {
        meter_resolve_SEM3_data1(auStack_130,0x270d14,0);
        meter_resolve_SEM3_data2(auStack_230,0x270d14,0);
        g_meter._20_4_ = g_meter._20_4_ + 1;
        g_meter[2] = '\x01';
        meterDispalyCountR = meterDispalyCountR + 1;
        system("echo 1 >/tmp/gridStatus.conf");
      }
      else {
        system("echo 0 >/tmp/gridStatus.conf");
        g_meter[2] = '\0';
      }
      if (g_meter._1192_4_ == 2) {
        local_24 = 3000;
      }
      else {
        local_24 = 0;
      }
      iVar1 = meter_get_data(g_meter[0x259],local_24,0x48,auStack_130,4);
      if ((iVar1 == 0) &&
         (iVar1 = meter_get_data(g_meter[0x259],local_24 + 0x15a,0xc,auStack_230,4), iVar1 == 0)) {
        meter_resolve_SEM3_data1(auStack_130,0x270f6c,1);
        meter_resolve_SEM3_data2(auStack_230,0x270f6c,1);
        g_meter._620_4_ = g_meter._620_4_ + 1;
        g_meter[0x25a] = '\x01';
        meterDispalyCountR = meterDispalyCountR + 1;
        system("echo 1 >/tmp/pvStatus.conf");
      }
      else {
        system("echo 0 >/tmp/pvStatus.conf");
        g_meter[0x25a] = '\0';
      }
    }
    else {
      if (g_meter._592_4_ == 2) {
        local_28 = 3000;
      }
      else {
        local_28 = 0;
      }
      iVar1 = meter_get_data(g_meter[1],local_28,0x48,auStack_130,4);
      if (iVar1 == 0) {
        meter_resolve_SEM3_data1(auStack_130,0x270d14,0);
        g_meter._20_4_ = g_meter._20_4_ + 1;
        g_meter[2] = '\x01';
        meterDispalyCountR = meterDispalyCountR + 1;
        system("echo 1 >/tmp/gridStatus.conf");
      }
      else {
        system("echo 0 >/tmp/gridStatus.conf");
        g_meter[2] = '\0';
      }
      if (g_meter._1192_4_ == 2) {
        local_28 = 3000;
      }
      else {
        local_28 = 0;
      }
      iVar1 = meter_get_data(g_meter[0x259],local_28,0x48,auStack_130,4);
      if (iVar1 == 0) {
        meter_resolve_SEM3_data1(auStack_130,0x270f6c,1);
        g_meter._620_4_ = g_meter._620_4_ + 1;
        g_meter[0x25a] = '\x01';
        meterDispalyCountR = meterDispalyCountR + 1;
        system("echo 1 >/tmp/pvStatus.conf");
      }
      else {
        system("echo 0 >/tmp/pvStatus.conf");
        g_meter[0x25a] = '\0';
      }
    }
  }
  else {
    local_18 = 0;
    local_11 = 0;
    while (local_11 < 2) {
      if (((*(int *)(g_meter + (uint)local_11 * 600 + 0xc) == 0) || (g_meter_switch == 0)) ||
         (*(int *)(g_meter + (uint)local_11 * 600 + 0x210) == 0)) {
        local_11 = local_11 + 1;
        local_18 = 0;
      }
      else {
        uVar2 = *(uint *)(g_meter + (uint)local_11 * 600 + 0x18);
        if (uVar2 < 0x1307) {
          if (uVar2 < 0x1305) {
            if (uVar2 < 0x1203) {
              if (uVar2 < 0x1201) {
                if (uVar2 - 0x1101 < 3) {
                  if (param_1 == 1) {
                    iVar1 = meter_get_data(g_meter[(uint)local_11 * 600 + 1],0x2000,0x10,auStack_130
                                           ,3);
                    if ((iVar1 == 0) &&
                       (iVar1 = meter_get_data(g_meter[(uint)local_11 * 600 + 1],0x4000,0xc,
                                               auStack_230,3), iVar1 == 0)) {
                      meter_resolve_DDSU666_data1
                                (auStack_130,(uint)local_11 * 600 + 0x270d14,local_11);
                      meter_resolve_DDSU666_data2
                                (auStack_230,(uint)local_11 * 600 + 0x270d14,local_11);
                      *(int *)(g_meter + (uint)local_11 * 600 + 0x14) =
                           *(int *)(g_meter + (uint)local_11 * 600 + 0x14) + 1;
                      g_meter[(uint)local_11 * 600 + 2] = 1;
                      meterDispalyCountR = meterDispalyCountR + 1;
                    }
                    else {
                      g_meter[(uint)local_11 * 600 + 2] = 0;
                    }
                  }
                  else {
                    iVar1 = meter_get_data(g_meter[(uint)local_11 * 600 + 1],0x2000,0x10,auStack_130
                                           ,3);
                    if (iVar1 == 0) {
                      meter_resolve_DDSU666_data1
                                (auStack_130,(uint)local_11 * 600 + 0x270d14,local_11);
                      *(int *)(g_meter + (uint)local_11 * 600 + 0x14) =
                           *(int *)(g_meter + (uint)local_11 * 600 + 0x14) + 1;
                      g_meter[(uint)local_11 * 600 + 2] = 1;
                      meterDispalyCountR = meterDispalyCountR + 1;
                    }
                    else {
                      g_meter[(uint)local_11 * 600 + 2] = 0;
                    }
                  }
                }
              }
              else {
LAB_000b3914:
                if (param_1 == 1) {
                  iVar1 = meter_get_data(g_meter[(uint)local_11 * 600 + 1],0x2006,0x42,auStack_130,3
                                        );
                  if ((iVar1 == 0) &&
                     (iVar1 = meter_get_data(g_meter[(uint)local_11 * 600 + 1],0x101e,0x12,
                                             auStack_230,3), iVar1 == 0)) {
                    meter_resolve_DTSU666_data1
                              (auStack_130,(uint)local_11 * 600 + 0x270d14,local_11);
                    meter_resolve_DTSU666_data2
                              (auStack_230,(uint)local_11 * 600 + 0x270d14,local_11);
                    *(int *)(g_meter + (uint)local_11 * 600 + 0x14) =
                         *(int *)(g_meter + (uint)local_11 * 600 + 0x14) + 1;
                    g_meter[(uint)local_11 * 600 + 2] = 1;
                    meterDispalyCountR = meterDispalyCountR + 1;
                  }
                  else {
                    g_meter[(uint)local_11 * 600 + 2] = 0;
                  }
                }
                else {
                  iVar1 = meter_get_data(g_meter[(uint)local_11 * 600 + 1],0x2006,0x42,auStack_130,3
                                        );
                  if (iVar1 == 0) {
                    meter_resolve_DTSU666_data1
                              (auStack_130,(uint)local_11 * 600 + 0x270d14,local_11);
                    *(int *)(g_meter + (uint)local_11 * 600 + 0x14) =
                         *(int *)(g_meter + (uint)local_11 * 600 + 0x14) + 1;
                    g_meter[(uint)local_11 * 600 + 2] = 1;
                    meterDispalyCountR = meterDispalyCountR + 1;
                  }
                  else {
                    g_meter[(uint)local_11 * 600 + 2] = 0;
                  }
                }
              }
            }
            else if (0x1300 < uVar2) goto LAB_000b3914;
          }
          else if (param_1 == 1) {
            iVar1 = meter_get_data(g_meter[(uint)local_11 * 600 + 1],0x2006,0x2c,auStack_130,3);
            if (((iVar1 == 0) &&
                (iVar1 = meter_get_data(g_meter[(uint)local_11 * 600 + 1],0x2044,2,auStack_330,3),
                iVar1 == 0)) &&
               (iVar1 = meter_get_data(g_meter[(uint)local_11 * 600 + 1],0x101e,0x12,auStack_230,3),
               iVar1 == 0)) {
              memcpy(auStack_b1,auStack_32d,4);
              meter_resolve_DTSU666_data1(auStack_130,(uint)local_11 * 600 + 0x270d14,local_11);
              meter_resolve_DTSU666_data2(auStack_230,(uint)local_11 * 600 + 0x270d14,local_11);
              *(int *)(g_meter + (uint)local_11 * 600 + 0x14) =
                   *(int *)(g_meter + (uint)local_11 * 600 + 0x14) + 1;
              g_meter[(uint)local_11 * 600 + 2] = 1;
              meterDispalyCountR = meterDispalyCountR + 1;
            }
            else {
              g_meter[(uint)local_11 * 600 + 2] = 0;
            }
          }
          else {
            iVar1 = meter_get_data(g_meter[(uint)local_11 * 600 + 1],0x2006,0x2c,auStack_130,3);
            if ((iVar1 == 0) &&
               (iVar1 = meter_get_data(g_meter[(uint)local_11 * 600 + 1],0x2044,2,auStack_130,3),
               iVar1 == 0)) {
              memcpy(auStack_b1,auStack_32d,4);
              meter_resolve_DTSU666_data1(auStack_130,(uint)local_11 * 600 + 0x270d14,local_11);
              *(int *)(g_meter + (uint)local_11 * 600 + 0x14) =
                   *(int *)(g_meter + (uint)local_11 * 600 + 0x14) + 1;
              g_meter[(uint)local_11 * 600 + 2] = 1;
              meterDispalyCountR = meterDispalyCountR + 1;
            }
            else {
              g_meter[(uint)local_11 * 600 + 2] = 0;
            }
          }
        }
        else if (uVar2 == 0x9997) {
          printmsg("ECUC_get_meter_data");
          iVar1 = metergetdata(auStack_130,2 - (uint)local_11);
          if (iVar1 == 1) {
            printhexmsg(&DAT_000e6444,auStack_128,0x50);
            meter_resolve_ECU_C_data1(auStack_130,(uint)local_11 * 600 + 0x270d14,local_11);
            if (param_1 == 1) {
              meter_resolve_ECU_C_data2(auStack_130,(uint)local_11 * 600 + 0x270d14,local_11);
            }
            *(int *)(g_meter + (uint)local_11 * 600 + 0x14) =
                 *(int *)(g_meter + (uint)local_11 * 600 + 0x14) + 1;
            g_meter[(uint)local_11 * 600 + 2] = 1;
            meterDispalyCountR = meterDispalyCountR + 1;
          }
          else {
            g_meter[(uint)local_11 * 600 + 2] = 0;
          }
        }
        else if (uVar2 < 0x9998) {
          if (uVar2 == 0x2101) {
            if (param_1 == 1) {
              iVar1 = meter_get_data(g_meter[(uint)local_11 * 600 + 1],0,0x24,auStack_130,4);
              if ((iVar1 == 0) &&
                 (iVar1 = meter_get_data(g_meter[(uint)local_11 * 600 + 1],0x46,6,auStack_230,4),
                 iVar1 == 0)) {
                meter_resolve_SEM1_data1(auStack_130,(uint)local_11 * 600 + 0x270d14,local_11);
                meter_resolve_SEM1_data2(auStack_230,(uint)local_11 * 600 + 0x270d14,local_11);
                *(int *)(g_meter + (uint)local_11 * 600 + 0x14) =
                     *(int *)(g_meter + (uint)local_11 * 600 + 0x14) + 1;
                g_meter[(uint)local_11 * 600 + 2] = 1;
                meterDispalyCountR = meterDispalyCountR + 1;
              }
              else {
                g_meter[(uint)local_11 * 600 + 2] = 0;
              }
            }
            else {
              iVar1 = meter_get_data(g_meter[(uint)local_11 * 600 + 1],0,0x24,auStack_130,4);
              if (iVar1 == 0) {
                meter_resolve_SEM1_data1(auStack_130,(uint)local_11 * 600 + 0x270d14,local_11);
                *(int *)(g_meter + (uint)local_11 * 600 + 0x14) =
                     *(int *)(g_meter + (uint)local_11 * 600 + 0x14) + 1;
                g_meter[(uint)local_11 * 600 + 2] = 1;
                meterDispalyCountR = meterDispalyCountR + 1;
              }
              else {
                g_meter[(uint)local_11 * 600 + 2] = 0;
              }
            }
          }
          else if (uVar2 == 0x2301) {
            if (param_1 == 1) {
              iVar1 = meter_get_data(g_meter[(uint)local_11 * 600 + 1],0,0x48,auStack_130,4);
              if ((iVar1 == 0) &&
                 (iVar1 = meter_get_data(g_meter[(uint)local_11 * 600 + 1],0x15a,0xc,auStack_230,4),
                 iVar1 == 0)) {
                meter_resolve_SEM3_data1(auStack_130,(uint)local_11 * 600 + 0x270d14,local_11);
                meter_resolve_SEM3_data2(auStack_230,(uint)local_11 * 600 + 0x270d14,local_11);
                *(int *)(g_meter + (uint)local_11 * 600 + 0x14) =
                     *(int *)(g_meter + (uint)local_11 * 600 + 0x14) + 1;
                g_meter[(uint)local_11 * 600 + 2] = 1;
                meterDispalyCountR = meterDispalyCountR + 1;
              }
              else {
                g_meter[(uint)local_11 * 600 + 2] = 0;
              }
            }
            else {
              iVar1 = meter_get_data(g_meter[(uint)local_11 * 600 + 1],0,0x48,auStack_130,4);
              if (iVar1 == 0) {
                meter_resolve_SEM3_data1(auStack_130,(uint)local_11 * 600 + 0x270d14,local_11);
                *(int *)(g_meter + (uint)local_11 * 600 + 0x14) =
                     *(int *)(g_meter + (uint)local_11 * 600 + 0x14) + 1;
                g_meter[(uint)local_11 * 600 + 2] = 1;
                meterDispalyCountR = meterDispalyCountR + 1;
              }
              else {
                g_meter[(uint)local_11 * 600 + 2] = 0;
              }
            }
          }
        }
        else if (uVar2 == 0x9998) {
          printmsg("net_get_meter_data");
          local_20 = GetMeterDataFromEMA(local_11,param_1,(uint)local_11 * 600 + 0x270e60,
                                         *(undefined4 *)(g_meter + (uint)local_11 * 600 + 0x20c));
          if (local_20 == 1) {
            meter_resolve_MDNS_data1((uint)local_11 * 600 + 0x270d14,local_11);
            meter_resolve_MDNS_data2((uint)local_11 * 600 + 0x270d14,local_11);
            *(int *)(g_meter + (uint)local_11 * 600 + 0x14) =
                 *(int *)(g_meter + (uint)local_11 * 600 + 0x14) + 1;
            g_meter[(uint)local_11 * 600 + 2] = 1;
            meterDispalyCountR = meterDispalyCountR + 1;
          }
          else if (local_20 == 2) {
            printfloatmsg(*(undefined4 *)(g_meter + (uint)local_11 * 600 + 0x94),"ActivePower[0]");
            printfloatmsg(*(undefined4 *)(g_meter + (uint)local_11 * 600 + 0x98),"ActivePower[1]");
            printfloatmsg(*(undefined4 *)(g_meter + (uint)local_11 * 600 + 0x9c),"ActivePower[2]");
            *(int *)(g_meter + (uint)local_11 * 600 + 0x14) =
                 *(int *)(g_meter + (uint)local_11 * 600 + 0x14) + 1;
            g_meter[(uint)local_11 * 600 + 2] = 1;
            meterDispalyCountR = meterDispalyCountR + 1;
          }
          else {
            g_meter[(uint)local_11 * 600 + 2] = 0;
          }
        }
        else if (uVar2 == 0x9999) {
          printmsg("mdns_get_meter_data");
          local_1c = send_and_reply((uint)local_11 * 600 + 0x270dec,(uint)local_11 * 600 + 0x270e1e,
                                    *(undefined4 *)(g_meter + (uint)local_11 * 600 + 0x20c));
          if (local_1c == 0) {
            g_meter[(uint)local_11 * 600 + 2] = 0;
          }
          else {
            meter_resolve_MDNS_data1((uint)local_11 * 600 + 0x270d14,local_11);
            if (param_1 == 1) {
              meter_resolve_MDNS_data2((uint)local_11 * 600 + 0x270d14,local_11);
            }
            *(int *)(g_meter + (uint)local_11 * 600 + 0x14) =
                 *(int *)(g_meter + (uint)local_11 * 600 + 0x14) + 1;
            g_meter[(uint)local_11 * 600 + 2] = 1;
            meterDispalyCountR = meterDispalyCountR + 1;
          }
        }
        if ((g_meter[(uint)local_11 * 600 + 2] == '\0') &&
           (*(int *)(g_meter + (uint)local_11 * 600 + 0x18) == 0x9999)) {
          if (local_18 < 2) {
            local_18 = local_18 + 1;
            sleep(2);
          }
          else {
            if (local_11 == 0) {
              system("echo 0 >/tmp/gridStatus.conf");
            }
            else {
              system("echo 0 >/tmp/pvStatus.conf");
            }
            local_11 = local_11 + 1;
            local_18 = 0;
          }
        }
        else {
          if (local_11 == 0) {
            if (g_meter[2] == '\0') {
              system("echo 0 >/tmp/gridStatus.conf");
            }
            else {
              system("echo 1 >/tmp/gridStatus.conf");
            }
          }
          else if (g_meter[(uint)local_11 * 600 + 2] == '\0') {
            system("echo 0 >/tmp/pvStatus.conf");
          }
          else {
            system("echo 1 >/tmp/pvStatus.conf");
          }
          local_11 = local_11 + 1;
          local_18 = 0;
        }
      }
      memset(auStack_130,0,0x100);
      memset(auStack_230,0,0x100);
    }
  }
  if ((g_meter._1128_4_ == 1) || (g_meter._528_4_ == 1)) {
    response_meterStatus();
  }
  if ((g_meter[0x25a] != '\0') || (g_meter[2] != '\0')) {
    saveMeterData();
  }
  return;
}



