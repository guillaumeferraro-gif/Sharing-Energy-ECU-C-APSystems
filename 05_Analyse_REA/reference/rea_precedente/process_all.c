/* REA ev_ca13d4f4eea749e3d08396da5d238a9ebbab3c1eaa7398069966a981ff617c18 */

undefined4 process_all(undefined4 param_1)

{
  int iVar1;
  
  if (g_meter_switch == 1) {
    readRmeter();
    update_system_modbus_buffer_meter();
  }
  if ((apstoreSWITCH == 1) && (systemEnable == 1)) {
    zeroExportSelect = 1;
    countercurrentWithPowerR2();
    relayControl();
  }
  else if ((apstoreSWITCH == 2) && (systemEnable == 1)) {
    zeroExportSelect = 1;
    relayControl();
  }
  else if ((timing_flag == 1) && (systemEnable == 1)) {
    zeroExportSelect = 2;
    timing_func_exec();
  }
  else if ((slave485 == 0) &&
          (((masterFlag != 1 && (g_meter_switch != 1)) && (serial485info._0_4_ != 1)))) {
    zeroExportSelect = 5;
    system("killall slave485");
    system("/home/applications/slave485 &");
    slave485 = 1;
  }
  iVar1 = access("/tmp/apstorgeSwitchOff.conf",0);
  if (iVar1 == 0) {
    apstoreSWITCH = get_ints_from_file("/etc/yuneng/apstorgeSwitch.conf");
    system("rm /tmp/apstorgeSwitchOff.conf");
    last_userpower = 0xffffffff;
  }
  processpower(param_1);
  mqtt_post_cause();
  process_turn_on_off(param_1);
  process_quick_boot(param_1);
  process_ipp(param_1);
  process_ird_all(param_1);
  process_ird(param_1);
  turn_on_off(param_1);
  clear_gfdi(param_1);
  set_protection_parameters(param_1);
  set_protection_parameters_inverter_one(param_1);
  save_A145_inverter_to_all();
  save_A161json_inverter_to_all();
  process_drm_power2(param_1);
  process_ZigBeeTransimission(param_1);
  auto_check_zb_channel();
  auto_change_zb_channel();
  auto_find_zb();
  set_get_xiao_onoff();
  setSystemMaxPower();
  setFromDAconf();
  clear_zigbee_rom();
  set_inverter_new(param_1,2);
  set_paras_pv();
  clear_slave_bind();
  iVar1 = get_ints_from_file("/tmp/set_sPfile.conf");
  if (iVar1 == 1) {
    set_paras_sPfile(param_1,0);
  }
  iVar1 = get_ints_from_file("/tmp/set_tPfile.conf");
  if (iVar1 == 1) {
    set_paras_tPfile(param_1,0);
  }
  iVar1 = get_ints_from_file("/etc/yuneng/quickPowerSwitch.conf");
  if (iVar1 == 1) {
    quickRefresh();
  }
  return 0;
}



