/* REA ev_cf8be2901d4407ce4e5670578b7153b0e17ec288876912d8e24a8b1eb702d1ea */

void meter_resolve_MDNS_data1(undefined4 *param_1,int param_2)

{
  *param_1 = 0x7f800000;
  param_1[1] = 0x7f800000;
  param_1[2] = 0x7f800000;
  param_1[0xb] = 0x7f800000;
  param_1[0xc] = 0x7f800000;
  param_1[0xd] = 0x7f800000;
  param_1[3] = 0x7f800000;
  param_1[4] = 0x7f800000;
  param_1[5] = 0x7f800000;
  param_1[6] = 0x7f800000;
  param_1[7] = 0x7f800000;
  param_1[8] = 0x7f800000;
  param_1[9] = 0x7f800000;
  param_1[10] = 0x7f800000;
  param_1[0xe] = 0x7f800000;
  param_1[0xf] = 0x7f800000;
  param_1[0x10] = 0x7f800000;
  param_1[0x11] = 0x7f800000;
  param_1[0x12] = 0x7f800000;
  if (getdata._24_4_ != 0) {
    *param_1 = getdata._28_4_;
  }
  if (getdata._32_4_ != 0) {
    param_1[1] = getdata._36_4_;
  }
  if (getdata._40_4_ != 0) {
    param_1[2] = getdata._44_4_;
  }
  if (getdata._48_4_ != 0) {
    param_1[0xb] = getdata._52_4_;
  }
  if (getdata._56_4_ != 0) {
    param_1[0xc] = getdata._60_4_;
  }
  if (getdata._64_4_ != 0) {
    param_1[0xd] = getdata._68_4_;
  }
  if (getdata._80_4_ != 0) {
    param_1[3] = getdata._84_4_;
    if (((int)((uint)((float)param_1[3] < 0.0) << 0x1f) < 0) &&
       ((float)param_1[0xb] <= 3.4028235e+38)) {
      param_1[0xb] = -ABS((float)param_1[0xb]);
    }
    if (((param_2 == 1) && ((float)param_1[3] <= 3.4028235e+38)) && ((float)param_1[3] != 0.0)) {
      if ((int)((uint)((float)param_1[3] < 0.0) << 0x1f) < 0) {
        g_meter._1168_4_ = 1;
      }
      else {
        g_meter._1168_4_ = 0;
      }
    }
  }
  if (getdata._88_4_ != 0) {
    param_1[4] = getdata._92_4_;
    if (((int)((uint)((float)param_1[4] < 0.0) << 0x1f) < 0) &&
       ((float)param_1[0xc] <= 3.4028235e+38)) {
      param_1[0xc] = -ABS((float)param_1[0xc]);
    }
    if (((param_2 == 1) && ((float)param_1[4] <= 3.4028235e+38)) && ((float)param_1[4] != 0.0)) {
      if ((int)((uint)((float)param_1[4] < 0.0) << 0x1f) < 0) {
        g_meter._1172_4_ = 1;
      }
      else {
        g_meter._1172_4_ = 0;
      }
    }
  }
  if (getdata._96_4_ != 0) {
    param_1[5] = getdata._100_4_;
    if (((int)((uint)((float)param_1[5] < 0.0) << 0x1f) < 0) &&
       ((float)param_1[0xd] <= 3.4028235e+38)) {
      param_1[0xd] = -ABS((float)param_1[0xd]);
    }
    if (((param_2 == 1) && ((float)param_1[5] <= 3.4028235e+38)) && ((float)param_1[5] != 0.0)) {
      if ((int)((uint)((float)param_1[5] < 0.0) << 0x1f) < 0) {
        g_meter._1176_4_ = 1;
      }
      else {
        g_meter._1176_4_ = 0;
      }
    }
  }
  if ((*(int *)(g_meter + param_2 * 600 + 0x238) != 0) && ((float)param_1[3] <= 3.4028235e+38)) {
    param_1[3] = -(float)param_1[3];
  }
  if ((*(int *)(g_meter + param_2 * 600 + 0x23c) != 0) && ((float)param_1[4] <= 3.4028235e+38)) {
    param_1[4] = -(float)param_1[4];
  }
  if ((*(int *)(g_meter + param_2 * 600 + 0x240) != 0) && ((float)param_1[5] <= 3.4028235e+38)) {
    param_1[5] = -(float)param_1[5];
  }
  if ((*(int *)(g_meter + param_2 * 600 + 0x238) != 0) && ((float)param_1[0xb] <= 3.4028235e+38)) {
    param_1[0xb] = -(float)param_1[0xb];
  }
  if ((*(int *)(g_meter + param_2 * 600 + 0x23c) != 0) && ((float)param_1[0xc] <= 3.4028235e+38)) {
    param_1[0xc] = -(float)param_1[0xc];
  }
  if ((*(int *)(g_meter + param_2 * 600 + 0x240) != 0) && ((float)param_1[0xd] <= 3.4028235e+38)) {
    param_1[0xd] = -(float)param_1[0xd];
  }
  if (getdata._72_4_ == 0) {
    if (phase == 1) {
      param_1[6] = param_1[3];
    }
    else {
      param_1[6] = (float)param_1[3] + (float)param_1[4] + (float)param_1[5];
    }
  }
  else {
    param_1[6] = getdata._76_4_;
    if ((param_2 == 1) && ((int)((uint)((float)param_1[6] < 0.0) << 0x1f) < 0)) {
      param_1[6] = -(float)param_1[6];
    }
  }
  if (getdata._112_4_ != 0) {
    param_1[7] = getdata._116_4_;
  }
  if (getdata._120_4_ != 0) {
    param_1[8] = getdata._124_4_;
  }
  if (getdata._128_4_ != 0) {
    param_1[9] = getdata._132_4_;
  }
  if ((*(int *)(g_meter + param_2 * 600 + 0x238) != 0) && ((float)param_1[7] <= 3.4028235e+38)) {
    param_1[7] = -(float)param_1[7];
  }
  if ((*(int *)(g_meter + param_2 * 600 + 0x23c) != 0) && ((float)param_1[8] <= 3.4028235e+38)) {
    param_1[8] = -(float)param_1[8];
  }
  if ((*(int *)(g_meter + param_2 * 600 + 0x240) != 0) && ((float)param_1[9] <= 3.4028235e+38)) {
    param_1[9] = -(float)param_1[9];
  }
  if (getdata._104_4_ == 0) {
    if (phase == 1) {
      param_1[10] = param_1[7];
    }
    else {
      param_1[10] = (float)param_1[7] + (float)param_1[8] + (float)param_1[9];
    }
  }
  else {
    param_1[10] = getdata._108_4_;
    if ((param_2 == 1) && ((int)((uint)((float)param_1[10] < 0.0) << 0x1f) < 0)) {
      param_1[10] = -(float)param_1[10];
    }
  }
  if (((*(int *)(g_meter + param_2 * 600 + 0x238) != 0) ||
      (*(int *)(g_meter + param_2 * 600 + 0x23c) != 0)) ||
     (*(int *)(g_meter + param_2 * 600 + 0x240) != 0)) {
    param_1[6] = 0;
    param_1[10] = 0;
    if ((float)param_1[3] <= 3.4028235e+38) {
      param_1[6] = (float)param_1[6] + (float)param_1[3];
      param_1[10] = (float)param_1[10] + (float)param_1[7];
    }
    if ((float)param_1[4] <= 3.4028235e+38) {
      param_1[6] = (float)param_1[6] + (float)param_1[4];
      param_1[10] = (float)param_1[10] + (float)param_1[8];
    }
    if ((float)param_1[5] <= 3.4028235e+38) {
      param_1[6] = (float)param_1[6] + (float)param_1[5];
      param_1[10] = (float)param_1[10] + (float)param_1[9];
    }
  }
  if (getdata._168_4_ != 0) {
    param_1[0x11] = getdata._172_4_;
  }
  if (getdata._176_4_ != 0) {
    param_1[0xe] = getdata._180_4_;
  }
  if (getdata._184_4_ != 0) {
    param_1[0xf] = getdata._188_4_;
  }
  if (getdata._192_4_ != 0) {
    param_1[0x10] = getdata._196_4_;
  }
  if (getdata._200_4_ != 0) {
    param_1[0x12] = getdata._204_4_;
  }
  printfloatmsg(*param_1,"Vol[0]");
  printfloatmsg(param_1[3],"ActivePower[0]");
  printfloatmsg(param_1[4],"ActivePower[1]");
  printfloatmsg(param_1[5],"ActivePower[2]");
  return;
}



