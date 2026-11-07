/* REA ev_07f4a70163131b2ef497fca229e502427ab5fee6dfe8f3f69a0217e27ff3688f */

void main(void)

{
  int iVar1;
  size_t sVar2;
  undefined4 uVar3;
  time_t tVar4;
  int extraout_r1;
  uint in_fpscr;
  undefined2 local_5c;
  undefined1 local_5a;
  undefined4 local_58;
  undefined4 local_54;
  int local_50;
  undefined4 local_4c;
  undefined4 local_48;
  int local_44;
  int local_40;
  int local_3c;
  time_t local_38;
  int local_34;
  int local_30;
  FILE *local_2c;
  int local_28;
  int local_24;
  int local_20;
  int local_1c;
  int local_18;
  int local_14;
  
  update_dEBUG_flag("main.c",6);
  ecu_mobile = get_int_from_file("/etc/yuneng/mobile.conf");
  local_2c = fopen("/etc/yuneng/polling_interval.conf","r");
  if (local_2c != (FILE *)0x0) {
    fgets(&stack0xffe430ac,0x14,local_2c);
    POLLING_INTERVAL = atoi(&stack0xffe430ac);
    fclose(local_2c);
    if (POLLING_INTERVAL < 1) {
      POLLING_INTERVAL = 300;
    }
    printdecmsg("POLLING_INTERVAL",POLLING_INTERVAL);
  }
  printdecmsg("ecu_mobile",ecu_mobile);
  local_2c = fopen("/etc/yuneng/mqtt_mqttSupportIdentifier.conf","r");
  if (local_2c != (FILE *)0x0) {
    memset(&stack0xffe42094,0,0x1000);
    fgets(&stack0xffe42094,0x1000,local_2c);
    fclose(local_2c);
    local_30 = cJSON_Parse(&stack0xffe42094);
    if ((local_30 != 0) && (iVar1 = cJSON_IsArray(local_30), iVar1 != 0)) {
      local_14 = 0;
      while (iVar1 = cJSON_GetArraySize(local_30), local_14 < iVar1) {
        local_34 = cJSON_GetArrayItem(local_30,local_14);
        if (((local_34 != 0) && (iVar1 = cJSON_IsString(local_34), iVar1 != 0)) &&
           (iVar1 = strncmp(*(char **)(local_34 + 0x10),"inverterData",0xc), iVar1 == 0)) {
          mqtt_flag = 1;
          break;
        }
        local_14 = local_14 + 1;
      }
      cJSON_Delete(local_30);
    }
  }
  printdecmsg("mqtt_flag",mqtt_flag);
  if (ecu_mobile == 1) {
    POLLING_INTERVAL = 900;
  }
  local_18 = 0;
  local_38 = 0xffff;
  local_5c = 0;
  local_5a = 0;
  local_3c = 0;
  headinverter = &stack0xffe431c0;
  memset(&stack0xffe430c0,0,0x100);
  printf("\nmain.exe %s          %s             %s\n",&DAT_000cfb80,"Oct 20 2026","13:02:07");
  printmsg("Start-------------------------------------------------");
  local_2c = fopen("/etc/yuneng/model.conf","r");
  if (local_2c != (FILE *)0x0) {
    fgets(&version_model,0x100,local_2c);
    print2msg("EcuModel",&version_model);
    if (((DAT_000f1ac0 == 'C') || (DAT_000f1ac0 == 'c')) ||
       ((DAT_000f1ac0 == 'R' || (DAT_000f1ac0 == 'r')))) {
      system("killall apbutton");
      system("/home/applications/apbutton &");
      if ((DAT_000f1ac0 == 'C') || (DAT_000f1ac0 == 'c')) {
        open_ccfd();
        iVar1 = set_cc_ct(0);
        if (iVar1 == 1) {
          printmsg("Init CT OK");
        }
        else {
          printmsg("Init CT Failed");
        }
      }
    }
    fclose(local_2c);
  }
  system("killall AutoUpInver");
  system("/home/applications/AutoUpInver &");
  system("killall timeCheck.exe");
  system("/home/applications/timeCheck.exe 300 &");
  system("killall dy.exe");
  system("/home/applications/dy.exe &");
  db_check("/home/record.db");
  db_check("/home/mqtt_record.db");
  init_all(&stack0xffe431c0);
  last_mesh_time = get_ints_from_file("/etc/yuneng/last_mesh_time.conf");
  printdecmsg("last_mesh_time",last_mesh_time);
  if ((((DAT_000f1ac0 == 'C') || (DAT_000f1ac0 == 'c')) || (DAT_000f1ac0 == 'R')) ||
     (DAT_000f1ac0 == 'r')) {
    system("killall drm_connect.exe");
    system("killall xiaocontrol_client");
    local_2c = fopen("/etc/yuneng/area.conf","r");
    if (local_2c != (FILE *)0x0) {
      fgets(&stack0xffe430c0,0x100,local_2c);
      sVar2 = strlen(&stack0xffe430c0);
      if ((&stack0xffe430bf)[sVar2] == '\n') {
        sVar2 = strlen(&stack0xffe430c0);
        (&stack0xffe430bf)[sVar2] = 0;
      }
      sVar2 = strlen(&stack0xffe430c0);
      iVar1 = strncmp(&stack0xffe430bd + sVar2,"SAA",3);
      if (iVar1 == 0) {
        system("/home/applications/drm_connect.exe &");
        init_xiaodatabase();
        system("/home/applications/xiaocontrol_client &");
      }
      fclose(local_2c);
    }
  }
  get_msleep();
  get_updateMoreUsleep();
  MAXOP = get_ints_from_file("/etc/yuneng/max_singlepanel_op.conf");
  if (MAXOP < 1) {
    MAXOP = 400;
  }
  uVar3 = get_time_hour();
  printdecmsg("get_time_hour()",uVar3);
  iVar1 = get_time_hour();
  if ((iVar1 < 3) && (iVar1 = get_ints_from_file("/etc/yuneng/emergencySwitch.conf"), iVar1 == 1)) {
    get_emergrncy_bin_info();
    DS3_upgrade(headinverter);
  }
  local_40 = get_ints_from_file("/etc/yuneng/DY_flag.conf");
  local_20 = 0;
  local_18 = time((time_t *)0x0);
  local_18 = local_18 + -300;
  next_upinverter = headinverter;
  set_params_wih_pvGrid_all();
  iVar1 = get_ints_from_file("/etc/yuneng/price_follow_switch.conf");
  if ((iVar1 == 1) &&
     (local_2c = fopen("/etc/yuneng/electr_price.conf","r"), local_2c != (FILE *)0x0)) {
    fseek(local_2c,4,0);
    fgets(eleprice,0x1000,local_2c);
    fclose(local_2c);
    get_time(&stack0xffe43098,&stack0xffe43094);
    local_44 = cJSON_Parse(eleprice);
    if (local_44 == 0) {
      system("echo 200 >/tmp/next_control_client_cmd.conf");
    }
    else {
      local_48 = cJSON_GetObjectItem(local_44,"EP_info");
      local_1c = 0;
      while (iVar1 = cJSON_GetArraySize(local_48), local_1c < iVar1) {
        local_4c = cJSON_GetArrayItem(local_48,local_1c);
        local_50 = cJSON_GetObjectItem(local_4c,"EP_date");
        iVar1 = strncmp(&stack0xffe43098,*(char **)(local_50 + 0x10),8);
        if (iVar1 == 0) break;
        local_1c = local_1c + 1;
      }
      iVar1 = cJSON_GetArraySize(local_48);
      if (iVar1 == local_1c) {
        system("echo 200 >/tmp/next_control_client_cmd.conf");
      }
      cJSON_Delete(local_44);
    }
  }
  serial485info._4_4_ = openserial(0x2580);
  if (serial485info._4_4_ != 0) {
    memset(&stack0xffe42094,0,0x800);
    local_54 = 0;
    CHNT_model_process(1,1);
    close(serial485info._4_4_);
    serial485info._4_4_ = 0;
  }
  turn_on_modbus();
  init_meterInfos_R();
  process_5min();
  get_system_model(&stack0xffe431c0,inverRA,inverRB,inverRC);
  init_status_of_three();
  init_meterInfos2_R();
  local_24 = 0;
  local_28 = 0;
  do {
    iVar1 = get_ints_from_file("/etc/yuneng/fast_polling.conf");
    if (iVar1 == 1) {
      printmsg("fast_polling...");
      memset(ecu + 0x48,0,0x10);
      local_58 = get_time(0xf7fcc,&local_5c);
      if (local_20 == 0) {
        local_20 = time((time_t *)0x0);
      }
      if ((0 < local_20) && (tVar4 = time((time_t *)0x0), 0xe10 < tVar4 - local_20)) {
        file_set_one(&DAT_000cfef0,"/etc/yuneng/fast_polling.conf");
        local_20 = 0;
      }
    }
    else if (local_20 != 0) {
      local_20 = 0;
    }
    local_38 = time((time_t *)0x0);
    if (((local_28 == 0) ||
        (iVar1 = get_ints_from_file("/etc/yuneng/fast_polling.conf"), iVar1 == 1)) ||
       ((__aeabi_idivmod(local_38,POLLING_INTERVAL), extraout_r1 < 0xf &&
        (POLLING_INTERVAL + -0xf < local_38 - local_18)))) {
      local_28 = 1;
      local_24 = 1;
      local_18 = time((time_t *)0x0);
      if (local_40 == 1) {
        changed_time(local_18,600);
      }
      update_counter_info(1);
      update_dEBUG_flag("main.c",6);
      zb_change_ecu_panid(1);
      get_config_of_zigbee2();
      memset(ecu + 0x48,0,0x10);
      local_3c = get_time_hour();
      process_5min();
      local_58 = get_time(0xf7fcc,&local_5c);
      if (POLLING_INTERVAL % 0x3c == 0) {
        ecu[0x55] = 0x30;
        ecu[0x54] = 0x30;
      }
      printmsg("****************************************");
      print2msg("ecu.broadcast_time",0xf7fcc);
      ecu._52_4_ = getalldata(&stack0xffe431c0,local_58,inverRA,inverRB);
      save_time_to_database(&stack0xffe431c0,local_58);
      ecu._40_4_ = (float)ecu._40_4_ + (float)ecu._44_4_;
      update_life_energy(ecu._40_4_);
      update_today_energy(ecu._44_4_);
      memset(&stack0xffe42094,0,100);
      sprintf(&stack0xffe42094,"echo \"%d\">/tmp/onlineNumber.conf",ecu._52_4_);
      system(&stack0xffe42094);
      if (0 < (int)ecu._52_4_) {
        uVar3 = VectorSignedToFloat(ecu._48_4_,(byte)(in_fpscr >> 0x16) & 3);
        save_system_power(uVar3,0xf7fcc);
        update_daily_energy(ecu._44_4_,0xf7fcc);
        update_monthly_energy(ecu._44_4_,0xf7fcc);
        update_yearly_energy(ecu._44_4_,0xf7fcc);
        update_lifetime_energy(ecu._40_4_);
        setRateProgress(100);
      }
      display_on_lcd_and_web();
      if ((0 < (int)ecu._52_4_) &&
         (iVar1 = get_ints_from_file("/etc/yuneng/fast_polling.conf"), iVar1 != 1)) {
        if (mqtt_flag == 1) {
          protocal_mqtt(&stack0xffe431c0,0xf7fcc);
        }
        else {
          protocol_APS18(&stack0xffe431c0,0xf7fcc);
        }
        saveevent(&stack0xffe431c0,0xf7fcc);
      }
      iVar1 = get_ints_from_file("/etc/yuneng/fast_polling.conf");
      if (iVar1 != 1) {
        if (mqtt_flag == 1) {
          protocol_mqtt_status(&stack0xffe431c0,0xf7fcc);
        }
        else {
          protocol_status(&stack0xffe431c0,0xf7fcc);
        }
      }
      protocol_status3(0xf7fcc);
      printdecmsg(&DAT_000cfa4c,0x3ef);
      displayonweb(&stack0xffe431c0,0xf7fcc);
      displaytoapp(&DAT_000cff58,&stack0xffe431c0,0xf7fcc,ecu._56_4_);
      format(ecu._44_4_,ecu._40_4_,&stack0xffe431c0,0xf7fcc,ecu._48_4_);
      iVar1 = get_int_from_file("/etc/yuneng/autoDelAlarm.conf");
      if (iVar1 == 1) {
        process_alarmDetection(&stack0xffe431c0);
      }
      detection_inverter_updated_bootstatus(&stack0xffe431c0);
      remote_update(&stack0xffe431c0,local_58);
      remote_update_autoUpInver();
      reset_inverter(&stack0xffe431c0);
      set_inverter_new(&stack0xffe431c0,0);
      if (((9 < local_3c) && (ecu._92_4_ == 1)) &&
         (get_inverter_shortaddress(&stack0xffe431c0), 3 < (int)ecu._96_4_)) {
        ecu._92_4_ = 0;
      }
      bind_nodata_inverter(&stack0xffe431c0);
      get_system_model(&stack0xffe431c0,inverRA,inverRB,inverRC);
      zbR_response();
      reset_bridge_zigbee();
    }
    iVar1 = get_ints_from_file("/etc/yuneng/fast_update.conf");
    if (iVar1 == 1) {
      remote_update_unicast(&stack0xffe431c0);
    }
    process_all(&stack0xffe431c0);
    usleep(100000);
    local_38 = time((time_t *)0x0);
    if (local_40 == 1) {
      changed_time(local_38,600);
    }
    iVar1 = get_ints_from_file("/etc/yuneng/fast_polling.conf");
    if ((iVar1 != 1) && (local_24 == 1)) {
      fill_up_data(&stack0xffe431c0,local_18 + 600,local_18,inverRA);
      local_24 = 0;
    }
  } while( true );
}



