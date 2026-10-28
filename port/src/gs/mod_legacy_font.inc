// Same approved font families, baked by ImGui at the actual requested display size.
// Keep this adapter name so all mod-menu/HUD callers use the sharp path together.
static bool mod_legacy_text(ImDrawList *d,const char *text,ImVec2 pos,float size,ImU32 colour,float maxWidth=0,bool centre=false,bool title=false){
 ImFont *font=title?sModTitleFont:sModBodyFont;if(!font||!text||size<=0)return false;
 ImVec2 bounds=font->CalcTextSizeA(size,100000.f,0,text);if(maxWidth>0&&bounds.x>maxWidth){size*=maxWidth/bounds.x;bounds=font->CalcTextSizeA(size,100000.f,0,text);}if(centre)pos.x-=bounds.x*.5f;
 float outline=title?size*.045f:size*.035f;if(outline<.65f)outline=.65f;
 const ImVec2 strokes[]={ImVec2(-outline,0),ImVec2(outline,0),ImVec2(0,-outline),ImVec2(0,outline)};
 for(const ImVec2 &at:strokes)d->AddText(font,size,ImVec2(pos.x+at.x,pos.y+at.y),IM_COL32(3,5,12,210),text);

 d->AddText(font,size,pos,colour,text);return true;
}
