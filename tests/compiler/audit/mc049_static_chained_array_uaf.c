/* MC-049: trigger ArrayType arena expansion inside chained designator. */
typedef int T01[1]; typedef int T02[2]; typedef int T03[3]; typedef int T04[4];
typedef int T05[5]; typedef int T06[6]; typedef int T07[7]; typedef int T08[8];
typedef int T09[9]; typedef int T10[10]; typedef int T11[11]; typedef int T12[12];
typedef int T13[13]; typedef int T14[14]; typedef int T15[15]; typedef int T16[16];
typedef int T17[17]; typedef int T18[18]; typedef int T19[19]; typedef int T20[20];
typedef int T21[21]; typedef int T22[22]; typedef int T23[23]; typedef int T24[24];
typedef int T25[25]; typedef int T26[26]; typedef int T27[27]; typedef int T28[28];
typedef int T29[29];
#ifndef MINIC_DROP30
typedef int T30[30];
#endif
#ifdef MINIC_EXTRA31
typedef int T31[31];
#endif
int values[2][2] = {[0][sizeof(int[3]) / sizeof(int) - 2] = 7};
int main(void) { return values[0][1] == 7 ? 7 : 99; }
