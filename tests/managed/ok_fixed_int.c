int main(void){
  int8  a = -1;   uint8  b = 255;
  int16 c = -2;   uint16 d = 40000;
  int32 e = -3;   uint32 f = 3000000000u;
  int64 g = -4;   uint64 h = 1;
  if (sizeof(int8)!=1||sizeof(int16)!=2||sizeof(int32)!=4||sizeof(int64)!=8) return 10;
  if (sizeof(uint8)!=1||sizeof(uint16)!=2||sizeof(uint32)!=4||sizeof(uint64)!=8) return 11;
  if (b!=255||d!=40000||g!=-4) return 12;
  return (a+c+e+(int)g==-10) ? 0 : 13;
}
