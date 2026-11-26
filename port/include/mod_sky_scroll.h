/* Exact reviewed scrolling cloud-sheet profiles, not a blanket stage/water speed change.
 * Fingerprints cover both raw texture pack and scrolling model/parameters. */
#ifndef MOD_SKY_SCROLL_H
#define MOD_SKY_SCROLL_H
static unsigned SS_Hash(const void*data,unsigned size){const unsigned char*p=data;unsigned h=2166136261u;while(size--)h=(h^*p++)*16777619u;return h;}
/* Res_RelocateOffsets writes three pointer fields; they are not asset identity. */
static unsigned SS_TexHash(const void*data,unsigned size){const unsigned char*p=data;unsigned i,h=2166136261u,n,entry;
 if(size<32)return 0;n=*(const unsigned*)p;entry=*(const unsigned*)(p+4);if(n<1||n>32||entry>size/4)return 0;entry*=4;if(entry>size||n>(size-entry)/64)return 0;
 for(i=0;i<size;i++){unsigned v=p[i];if((i>=16&&i<20)||(i>=entry&&i<entry+n*64&&((i-entry)%64)>=56))v=0;h=(h^v)*16777619u;}return h;
}
typedef struct {int stage;unsigned textureBytes,scrollBytes,textureHash,scrollHash;} SsProfile;
static const SsProfile sSkyProfiles[]={
 {2,66944u,3776u,0x01dc049du,0x0c8023cdu},
 {29,66944u,3776u,0x0f3f2030u,0x481c4680u},
 {0,66944u,3776u,0x0569f320u,0x04c06b52u},
 {1,66944u,3776u,0xba72ba6au,0x5729a938u},
 {3,66944u,3776u,0x2fa174dbu,0xf88c3d7cu},
 {6,66944u,9408u,0x48858b38u,0x851aeeafu},
 {7,66944u,3776u,0xddce1aadu,0xc1bad585u},
 {9,66944u,3776u,0x831d963fu,0xb6d9600eu},
 {10,66944u,3776u,0x5af68a6au,0x9bc6f4adu},
 {11,66944u,3776u,0x3aecaa80u,0x02c278e4u},
 {12,66944u,3776u,0x3aecaa80u,0x9a36bc26u},
 {13,66944u,3776u,0x3aecaa80u,0x3cede1f7u},
 {14,33216u,3776u,0x8e7e25c7u,0x4e6cb280u},
 {15,33216u,3776u,0x8e7e25c7u,0x12b685f6u},
 {17,16832u,5376u,0xc1e2e59cu,0xab30c87cu},
 {19,66944u,9408u,0xc19f362au,0xa007611cu},
 {20,66944u,3776u,0x0569f320u,0x04c06b52u},
 {21,66944u,3776u,0x829085e5u,0x70d6fe8eu},
 {22,66944u,3776u,0xf6239665u,0xad989b4du},
 {23,66944u,3776u,0x25c9195cu,0x859add4au},
 {24,66944u,3776u,0x01dc049du,0x310afa46u},
 {26,33216u,3776u,0x8e7e25c7u,0x4e6cb280u},
 {27,66944u,3776u,0xf5703c36u,0x66501305u},
 {28,66944u,9408u,0xc19f362au,0xa007611cu},
 {30,66944u,5312u,0xc2827a72u,0xd1b27439u},
 {31,66944u,9408u,0xc19f362au,0xa007611cu},
 {34,66944u,3776u,0x49a1c963u,0xc746bb14u},
 {40,66944u,3776u,0x49983d40u,0xb6d9600eu},
};
static int SS_Verified(int stage,const void*tex,unsigned tn,const void*scroll,unsigned sn){unsigned i;
 if(!tex||!scroll||tn<32||tn>2097152||sn<32||sn>1048576)return 0;
 for(i=0;i<sizeof(sSkyProfiles)/sizeof(*sSkyProfiles);i++){const SsProfile*p=sSkyProfiles+i;if(p->stage==stage&&p->textureBytes==tn&&p->scrollBytes==sn)return SS_TexHash(tex,tn)==p->textureHash&&SS_Hash(scroll,sn)==p->scrollHash;}return 0;
}
#endif
