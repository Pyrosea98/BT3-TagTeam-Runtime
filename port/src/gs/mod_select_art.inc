// Original-Tenkaichi-inspired generated sprites. One texture upload, no per-frame IO.
static SDL_GPUTexture *ms_kit_texture(){
 static SDL_GPUTexture*t;static bool tried;if(tried)return t;tried=true;
 char path[1024];snprintf(path,sizeof(path),"%s/beta8/mod-select/ui/select-kit-v1.rgba",Port_FileRoot());
 FILE*f=fopen(path,"rb");if(!f)f=fopen("mod-select/select-kit-v1.rgba","rb");if(!f)return nullptr;
 uint32_t wh[2];if(fread(wh,4,2,f)!=2||wh[0]!=1024||wh[1]!=1024){fclose(f);return nullptr;}
 auto*p=(uint8_t*)SDL_malloc(1024*1024*4);if(!p){fclose(f);return nullptr;}
 bool ok=fread(p,1,1024*1024*4,f)==1024*1024*4&&fgetc(f)==EOF;fclose(f);
 if(ok)t=Ui_Texture(p,1024,1024);else SDL_free(p);return t;
}
static bool ms_kit_sprite(ImDrawList*d,int id,ImVec2 p,ImVec2 size,ImU32 tint=IM_COL32_WHITE){
 auto*t=ms_kit_texture();if(!t||id<0||id>15)return false;
 ImVec2 uv((id%4)*.25f,(id/4)*.25f);
 d->AddImage(ImTextureRef((ImTextureID)(intptr_t)t),p,ImVec2(p.x+size.x,p.y+size.y),uv,ImVec2(uv.x+.25f,uv.y+.25f),tint);return true;
}
static bool ms_kit_number(ImDrawList*d,int number,ImVec2 p,float h){
 if(number<0||number>99||!ms_kit_texture())return false;
 if(number>=10){ms_kit_sprite(d,number/10,p,ImVec2(h,h));p.x+=h*.65f;}
 return ms_kit_sprite(d,number%10,p,ImVec2(h,h));
}
