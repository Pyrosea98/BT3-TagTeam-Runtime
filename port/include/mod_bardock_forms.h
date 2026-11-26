#ifndef MOD_BARDOCK_FORMS_H
#define MOD_BARDOCK_FORMS_H

/* Physical donor243 costume3 is the original red-haired God in ISO438.
   Byte targets retain donor IDs; the logical resolver translates243 to329. */
#define B8_BARDOCK_GOD 329
static int B8BardockFamily(int id){return id==73||id==176||id==314||id==315||id==199||id==B8_BARDOCK_GOD;}
static int B8BardockPatch(int id,unsigned char *p){
 int j;
 if(!B8BardockFamily(id))return 0;
 /* Match Goku's advancing list. Left is the alternate God branch;
    the fifth slot supplies the first remaining upgrade on neutral R3. */
 for(j=0;j<4;j++){p[0x98+j]=255;p[0x9c+j]=0;p[0xa0+j]=0;p[0xa4+j]=0;p[0xa8+j]=255;}
 p[0xac]=255;
 if(id==176)return 1; /* Great Ape is terminal, including inherited returns. */
 if(id==73||id==314||id==315){p[0x98]=243;p[0x9c]=1;p[0xa4]=2;p[0xac]=4;}
 if(id==73){
  p[0x99]=232;p[0x9d]=1;p[0xa5]=2;
  p[0x9a]=176;p[0x9e]=1;p[0xa2]=2;p[0xa6]=3;
  p[0x9b]=199;p[0x9f]=1;p[0xa7]=2;
 }else{
  if(id==314){p[0x99]=199;p[0x9d]=1;p[0xa5]=2;}
  p[0x9b]=73;p[0xa3]=1;p[0xa7]=1;
 }
 return 1;
}
static int B8BardockExtraField(int id,int slot,int field){
 static const int ssj1[]={165,1,0,2,255},ssj3[]={232,1,0,2,255},ssj4[]={199,1,0,2,255};
 const int *v=id==73?ssj1:id==314?ssj3:id==315?ssj4:0;
 return v&&slot==4&&field>=0&&field<5?v[field]:-1;
}
#endif
