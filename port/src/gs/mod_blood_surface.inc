/* Private, audited UV profiles. These marks are composited into a texture
 * variant used by the original model triangles: native skinning, clipping,
 * depth tests, lighting and visibility then all remain authoritative. */
static SDL_Surface *sBloodAtlas;
static int sBloodAtlasTried;
static int blood_atlas_load(void) {
    const char *path;SDL_Surface *input;
    if(sBloodAtlas)return 1;
    if(sBloodAtlasTried)return 0;
    sBloodAtlasTried=1;path=getenv("BT3_BLOOD_DECAL_ATLAS");
    if(!path)path="assets/fx/dbz-wounds-atlas-v1.png";
    input=SDL_LoadPNG(path);
    if(input){sBloodAtlas=SDL_ConvertSurface(input,SDL_PIXELFORMAT_RGBA32);SDL_DestroySurface(input);}
    if(!sBloodAtlas)fprintf(stderr,"bt3: blood surface atlas unavailable: %s\n",path);
    return sBloodAtlas!=NULL;
}
static int blood_skin(unsigned int pixel,int profile) {
    int r=pixel&255,g=(pixel>>8)&255,b=(pixel>>16)&255;
    /* TCC=RGB model textures legitimately have zero source alpha. It is not
     * transparency; preserve that byte and let the native pass interpret it. */
    if(profile==1)return (g>40&&g*10>r*13&&g*10>b*13)||
        (r>90&&g*100>r*45&&g*100<r*85&&b*100>r*40&&b*100<r*90);
    return r>70&&g*100>r*55&&g*100<r*92&&b*100>r*35&&b*100<r*85;
}
static int blood_stamp(unsigned int *pixels,int w,int h,int profile,int cell,
                       int x0,int y0,int sw,int sh,unsigned long long state) {
    int x,y,count=0;unsigned int tint[3]={(state>>16)&255,(state>>24)&255,(state>>32)&255};
    for(y=0;y<sh;y++)for(x=0;x<sw;x++) {
        int dx=x0+x,dy=y0+y,ax,ay,c,alpha;unsigned int old,result,lum;const unsigned char *p;
        if(dx<0||dx>=w||dy<0||dy>=h)continue;
        old=pixels[dy*w+dx];if(!blood_skin(old,profile))continue;
        ax=(int)(((cell%4)+(x+.5f)/sw)*sBloodAtlas->w/4.f);
        ay=(int)(((cell/4)+(y+.5f)/sh)*sBloodAtlas->h/4.f);
        if(ax<0||ay<0||ax>=sBloodAtlas->w||ay>=sBloodAtlas->h)continue;
        p=(const unsigned char*)sBloodAtlas->pixels+ay*sBloodAtlas->pitch+ax*4;
        if(p[3]<16)continue;alpha=p[3]*230/255;if(!alpha)continue;
        lum=p[0];if(p[1]>lum)lum=p[1];if(p[2]>lum)lum=p[2];
        result=old&0xFF000000u; /* retain native GS alpha/pass identity */
        for(c=0;c<3;c++){
            unsigned int v=((((old>>(c*8))&255)*(255-alpha)+(lum*tint[c]/255)*alpha)+127)/255;
            result|=v<<(c*8);
        }
        pixels[dy*w+dx]=result;count++;
    }
    return count;
}
typedef struct BloodFacePoint { int x,y; } BloodFacePoint;

/* All positions use inspected texels on the same exposed side of the face.
 * Compute one opacity per texel so segment joins don't leave dark dots. */
static int blood_face_trail(unsigned int *pixels,int w,int h,unsigned long long state,
                            const BloodFacePoint *points,int count,int strength,float falloff) {
    unsigned int tint[3]={(state>>16)&255,(state>>24)&255,(state>>32)&255};
    int x0=w,x1=0,y0=h,y1=0,changed=0;
    for(int i=0;i<count;i++){
        if(points[i].x-2<x0)x0=points[i].x-2;
        if(points[i].x+2>x1)x1=points[i].x+2;
        if(points[i].y-2<y0)y0=points[i].y-2;
        if(points[i].y+2>y1)y1=points[i].y+2;
    }
    for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){
        float coverage=0;
        if(x<0||y<0||x>=w||y>=h)continue;
        unsigned int old=pixels[y*w+x];
        if(!blood_skin(old,5))continue;
        for(int i=1;i<count;i++){
            float dx=points[i].x-points[i-1].x,dy=points[i].y-points[i-1].y;
            float px=x-points[i-1].x,py=y-points[i-1].y;
            float t=(dx*px+dy*py)/(dx*dx+dy*dy);
            if(t<0)t=0;else if(t>1)t=1;
            px-=t*dx;py-=t*dy;
            float value=1.f-(px*px+py*py)*falloff;
            if(value>coverage)coverage=value;
        }
        if(coverage<=0)continue;
        unsigned int alpha=(unsigned int)(coverage*(y%9==0?strength*3/4:strength));
        unsigned int result=old&0xFF000000u;
        for(unsigned c=0;c<3;c++)
            result|=(((old>>(c*8)&255)*(255-alpha)+tint[c]*alpha+127)/255)<<(c*8);
        pixels[y*w+x]=result;
        changed+=result!=old;
    }
    return changed;
}

static int blood_family_face(unsigned int *pixels,int w,int h,unsigned long long state,unsigned int hash) {
    static const BloodFacePoint gohan_eye[]={{18,83},{17,96},{18,112},{21,127}};
    static const BloodFacePoint gohan_cheek[]={{24,143},{29,153},{34,161}};
    static const BloodFacePoint gohan_mouth[]={{60,204},{56,208},{55,216},{50,229}};
    static const BloodFacePoint vegeta_eye[]={{23,48},{20,54},{21,63},{25,70}};
    static const BloodFacePoint vegeta_cheek[]={{31,76},{38,80},{42,81}};
    static const BloodFacePoint vegeta_mouth[]={{62,94},{58,98},{58,103},{53,112}};
    const BloodFacePoint *eye,*cheek,*mouth;
    int level=state&3,n;
    if(h==256&&(hash==0xC7CA7CBCu||hash==0xCF001ACDu||hash==0x7206AEF5u||hash==0x64A64827u||hash==0x3D55A8C0u||hash==0x20A7481Cu||hash==0xA68053FFu||hash==0x83E1A230u||hash==0x27630717u||hash==0x8521E9FCu)){
        eye=gohan_eye;cheek=gohan_cheek;mouth=gohan_mouth;
        n=blood_stamp(pixels,w,h,5,0,77,143,14,9,state);
        if(level>=2)n+=blood_stamp(pixels,w,h,5,9,63,198,13,7,state);
    }else if(h==128&&(hash==0x0811174Du||hash==0xF9593033u||hash==0x9F1B154Au||hash==0x2720DBA5u||hash==0x8A62ECE8u||hash==0x1AC79BF1u||hash==0x49186B34u||hash==0xA836B142u||hash==0x7308CCBDu||hash==0x07C14965u||hash==0x4E7DFADDu||hash==0x629AC02Au||hash==0x7B6D1F1Bu||hash==0x7E65FD52u||hash==0x83485B34u||hash==0x87B847BAu||hash==0xBE926A8Bu||hash==0xC01A5685u)){
        eye=vegeta_eye;cheek=vegeta_cheek;mouth=vegeta_mouth;
        n=blood_stamp(pixels,w,h,5,0,78,71,13,8,state);
        if(!n)n=blood_stamp(pixels,w,h,5,0,85,72,13,8,state);
        if(level>=2){
            int added=blood_stamp(pixels,w,h,5,9,61,91,13,7,state);
            if(!added)added=blood_stamp(pixels,w,h,5,9,70,96,13,7,state);
            n+=added;
        }
    }else return 0;
    if(level>=3){
        n+=blood_face_trail(pixels,w,h,state,eye,4,150,.89f);
        n+=blood_face_trail(pixels,w,h,state,cheek,3,125,.92f);
        n+=blood_face_trail(pixels,w,h,state,mouth,4,206,.49f);
    }
    if(n>0&&getenv("BT3_BLOOD_SURFACE_DUMP")){
        static unsigned int last_hash[2],last_level[2];
        unsigned index=h==128;
        if(last_hash[index]!=hash||last_level[index]!=(unsigned)level){
            last_hash[index]=hash;last_level[index]=level;
            fprintf(stderr,"bt3: family face painted hash%08x level%d pixels%d\n",hash,level,n);
        }
    }
    return n;
}

static unsigned int blood_surface_expected(int w,int h,int profile,int part) {
    unsigned int expected=0;
    if(part==48&&profile==1&&w==256&&h==128)expected=0x1318e64Du;
    if(part==48&&profile==2&&w==128&&h==256)expected=0x52A62942u;
    if(part==48&&profile==3&&w==128&&h==128)expected=0x7B0C6ECAu;
    if(part==48&&profile==4&&w==128&&h==256)expected=0xC2973D8Au;
    if(profile==1&&w==256&&h==128&&(part==19||part==33))expected=0x9E51C308u;
    if(profile==2&&w==256&&h==128&&(part==9||part==13))expected=0xF870DB75u;
    if(profile==2&&w==256&&h==128&&(part==19||part==33))expected=0xF8E04CF5u;
    if(profile==3&&w==128&&h==128&&(part==19||part==33))expected=0x6D0F9762u;
    if(profile==3&&w==128&&h==128&&(part==22||part==36))expected=0x53BBBBF0u;
    if(profile==4&&w==256&&h==128&&(part==19||part==33||part==22||part==36))expected=0x9D535E59u;
    return expected;
}

static int blood_surface_family_sheet(int w,int h,int part) {
    return part==48&&w==128&&(h==128||h==256);
}

static int blood_surface_may_paint(int w,int h,unsigned long long state,int part) {
    int profile=(state>>8)&255;
    return (state&3)&&profile&&(blood_surface_family_sheet(w,h,part)||
        blood_surface_expected(w,h,profile,part));
}

static int blood_surface_apply(unsigned int *pixels,int w,int h,unsigned long long state,int part) {
    int level=state&3,profile=(state>>8)&255,n=0,damaged=0;
    unsigned int expected=0,hash=2166136261u;int i,c;
    if(!level||!profile)return 0;
    if(blood_surface_family_sheet(w,h,part)){
        for(i=0;i<w*h;i++)for(c=0;c<3;c++)hash=(hash^((pixels[i]>>(c*8))&255))*16777619u;
        n=blood_atlas_load()?blood_family_face(pixels,w,h,state,hash):0;
        if(n)return n;
        hash=2166136261u;
    }
    if(profile==5)return 0;
    /* The UV recipe is valid only for an inspected native RGB sheet.
     * Explicit damaged alternatives preserve placement across model reloads.
     * Dimensions alone never admit a different texture. */
    expected=blood_surface_expected(w,h,profile,part);
    if(!expected)return 0;
    for(i=0;i<w*h;i++)for(c=0;c<3;c++)hash=(hash^((pixels[i]>>(c*8))&255))*16777619u;
    if(hash!=expected) {
        /* Each alternative was checked against its normal model draw packet.
         * Keep this mapping local to the corresponding profile and part. */
        switch(expected) {
        case 0x1318e64Du:
            if(hash==0xB6FD83E6u||hash==0x32CA40B5u||hash==0xDFDCA5C0u||hash==0xEEB69506u)expected=hash;
            break;
        case 0x9E51C308u: if(hash==0x5638D64Au||hash==0xC18F83B8u)expected=hash;break;
        case 0x52A62942u: if(hash==0x626DAAB5u)expected=hash;break;
        case 0xF870DB75u: if(hash==0x498560FCu)expected=hash;break;
        case 0x7B0C6ECAu:
            if(hash==0xDB17CA74u||hash==0xED552D8Bu||hash==0xF9973E3Cu)expected=hash;
            break;
        case 0xC2973D8Au: if(hash==0x9FED4C82u)expected=hash;break;
        case 0x9D535E59u: if(hash==0xF6634F7Au)expected=hash;break;
        }
        damaged=hash==expected;
    }
    if(hash!=expected||!blood_atlas_load())return 0;
    if(part==48&&profile==1&&w==256&&h==128) {
        /* Piccolo: keep the approved brow-side location. The mouth is in
         * the first face island, not the expression strip on the right. */
        n+=blood_stamp(pixels,w,h,profile,9,58,98,14,8,state);
        if(!n)n+=blood_stamp(pixels,w,h,profile,9,64,96,14,8,state);
        if(level>=2){n+=blood_stamp(pixels,w,h,profile,12,20,25,18,18,state);
                     n+=blood_stamp(pixels,w,h,profile,0,28,76,20,10,state);}
        if(level>=3){n+=blood_stamp(pixels,w,h,profile,3,82,77,20,11,state);
                     n+=blood_stamp(pixels,w,h,profile,5,45,84,15,10,state);}
    } else if(part==48&&profile==2&&w==128&&h==256) {
        /* Raditz: short mouth-corner smear and scattered cheek abrasions.
         * No pair of vertical trails underneath the eyes. */
        n+=blood_stamp(pixels,w,h,profile,9,68,205,17,8,state);
        if(level>=2){n+=blood_stamp(pixels,w,h,profile,0,24,178,20,11,state);
                     n+=blood_stamp(pixels,w,h,profile,2,84,180,18,10,state);}
        if(level>=3){n+=blood_stamp(pixels,w,h,profile,4,42,189,18,12,state);
                     n+=blood_stamp(pixels,w,h,profile,13,34,131,17,12,state);}
    } else if(part==48&&profile==3&&w==128&&h==128) {
        n+=blood_stamp(pixels,w,h,profile,9,63,96,15,8,state);
        if(level>=2){n+=blood_stamp(pixels,w,h,profile,0,20,73,20,12,state);
                     n+=blood_stamp(pixels,w,h,profile,12,20,26,18,15,state);}
        if(level>=3){n+=blood_stamp(pixels,w,h,profile,3,86,78,20,12,state);
                     n+=blood_stamp(pixels,w,h,profile,4,36,87,17,11,state);}
    } else if(part==48&&profile==4&&w==128&&h==256) {
        n+=blood_stamp(pixels,w,h,profile,9,64,111,15,8,state);
        if(level>=2){n+=blood_stamp(pixels,w,h,profile,0,22,92,20,11,state);
                     n+=blood_stamp(pixels,w,h,profile,12,20,66,18,14,state);}
        if(level>=3){n+=blood_stamp(pixels,w,h,profile,3,86,94,20,11,state);
                     n+=blood_stamp(pixels,w,h,profile,4,43,103,17,11,state);}
    } else if(level>=3&&profile==1&&w==256&&h==128&&
             (part==19||part==33)) {
        /* Audited green/pink bare-arm sheet. No marks on the purple sleeve,
         * cape, trousers or boots, which use separate native sheets. */
        n+=blood_stamp(pixels,w,h,profile,4,50,28,22,14,state);
        n+=blood_stamp(pixels,w,h,profile,2,172,88,21,13,state);
    } else if(level>=3&&profile==2&&w==256&&h==128&&
             (part==9||part==13)) {
        n+=blood_stamp(pixels,w,h,profile,5,37,44,21,14,state);
        n+=blood_stamp(pixels,w,h,profile,1,162,65,20,13,state);
    } else if(level>=3&&profile==2&&w==256&&h==128&&
             (part==19||part==33)) {
        n+=blood_stamp(pixels,w,h,profile,4,42,54,22,14,state);
        n+=blood_stamp(pixels,w,h,profile,3,176,65,20,12,state);
    } else if(level>=3&&profile==3&&w==128&&h==128&&(part==19||part==33)) {
        n+=blood_stamp(pixels,w,h,profile,4,35,52,22,14,state);
        n+=blood_stamp(pixels,w,h,profile,1,76,65,20,13,state);
    } else if(level>=3&&profile==3&&w==128&&h==128&&(part==22||part==36)) {
        n+=blood_stamp(pixels,w,h,profile,2,32,38,17,12,state);
    } else if(level>=3&&profile==4&&w==256&&h==128&&(part==19||part==33)) {
        n+=blood_stamp(pixels,w,h,profile,4,35,52,22,14,state);
        n+=blood_stamp(pixels,w,h,profile,1,76,65,20,13,state);
    } else if(level>=3&&profile==4&&w==256&&h==128&&(part==22||part==36)) {
        n+=blood_stamp(pixels,w,h,profile,2,166,38,17,12,state);
    }
    if(damaged&&n>0&&part==48&&getenv("BT3_BLOOD_SURFACE_DUMP")) {
        static unsigned int reported[5];
        if(!(reported[profile]&(1u<<level))) {
            reported[profile]|=1u<<level;
            fprintf(stderr,"bt3: damaged face painted profile%d hash%08x level%d pixels%d\n",
                    profile,hash,level,n);
        }
    }
    return n;
}
