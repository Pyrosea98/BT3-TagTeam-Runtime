
/* Shared PlayStation hints. Geometry is independent of texture/font coverage. */
enum ModPadGlyph {MP_CROSS,MP_TRIANGLE,MP_SQUARE,MP_CIRCLE,MP_DPAD,MP_LEFT,MP_RIGHT,MP_LR,MP_R3,MP_SELECT,MP_L1R1,MP_L2R2,MP_L2,MP_START};
struct ModPadHint {int button;const char *label;};
static float mod_pad_width(int kind){return kind==MP_START?37.f:kind==MP_SELECT?42.f:(kind==MP_L1R1||kind==MP_L2R2)?56.f:kind==MP_R3||kind==MP_L2?25.f:18.f;}
static void mod_pad_glyph(ImDrawList*d,int kind,ImVec2 c,float k){
 const ImU32 white=IM_COL32(235,231,238,255);float rr=6*k,th=SDL_max(1.f,1.65f*k);
 auto line=[&](float x,float y,float a,float b,ImU32 col){d->AddLine(ImVec2(c.x+(x+1)*k,c.y+(y+1)*k),ImVec2(c.x+(a+1)*k,c.y+(b+1)*k),IM_COL32(0,0,0,190),th+1);d->AddLine(ImVec2(c.x+x*k,c.y+y*k),ImVec2(c.x+a*k,c.y+b*k),col,th);};
 auto arrow=[&](float dx,int dir){line(dx-5*dir,0,dx+4*dir,0,white);line(dx+4*dir,0,dx, -4,white);line(dx+4*dir,0,dx,4,white);};
 auto pill=[&](const char*t,float dx,float w){ImVec2 a(c.x+(dx-w*.5f)*k,c.y-7*k),b(c.x+(dx+w*.5f)*k,c.y+7*k);d->AddRectFilled(a,b,IM_COL32(15,12,22,165),3*k);d->AddRect(a,b,white,3*k,0,th);ImFont*f=ImGui::GetFont();float fs=9*k;ImVec2 z=f->CalcTextSizeA(fs,1000,0,t);d->AddText(f,fs,ImVec2(c.x+dx*k-z.x*.5f,c.y-z.y*.5f),white,t);};
 switch(kind){
 case MP_CROSS:line(-6,-6,6,6,IM_COL32(103,175,255,255));line(-6,6,6,-6,IM_COL32(103,175,255,255));break;
 case MP_TRIANGLE:d->AddTriangle(ImVec2(c.x,c.y-rr),ImVec2(c.x-rr,c.y+rr),ImVec2(c.x+rr,c.y+rr),IM_COL32(84,229,151,255),th);break;
 case MP_SQUARE:d->AddRect(ImVec2(c.x-rr,c.y-rr),ImVec2(c.x+rr,c.y+rr),IM_COL32(237,132,208,255),0,0,th);break;
 case MP_CIRCLE:d->AddCircle(c,rr,IM_COL32(251,116,106,255),24,th);break;
 case MP_DPAD:line(-7,0,7,0,white);line(0,-7,0,7,white);line(-7,0,-4,-3,white);line(-7,0,-4,3,white);line(7,0,4,-3,white);line(7,0,4,3,white);line(0,-7,-3,-4,white);line(0,-7,3,-4,white);line(0,7,-3,4,white);line(0,7,3,4,white);break;
 case MP_LEFT:arrow(0,-1);break;case MP_RIGHT:arrow(0,1);break;case MP_LR:arrow(-5,-1);arrow(5,1);break;
 case MP_START:pill("START",0,35);break;
 case MP_R3:pill("R3",0,23);break;case MP_SELECT:pill("SELECT",0,40);break;
 case MP_L1R1:pill("L1",-15,24);pill("R1",15,24);break;
 case MP_L2R2:pill("L2",-15,24);pill("R2",15,24);break;
 case MP_L2:pill("L2",0,23);break;
 }
}
/* Width-aware packing also works on4:3; each complete icon/label stays together. */
static float mod_pad_hints(ImDrawList*d,ImVec2 pos,float width,float k,const ModPadHint*items,int n,float fs=16){
 ImFont*f=sModBodyFont?sModBodyFont:ImGui::GetFont();float x=pos.x,y=pos.y,h=(fs+9)*k;fs*=k;
 for(int i=0;i<n;i++){
  float icon=mod_pad_width(items[i].button)*k,label=f->CalcTextSizeA(fs,10000,0,items[i].label).x,w=icon+8*k+label+20*k;
  if(x>pos.x&&x+w>pos.x+width){x=pos.x;y+=h;}
  mod_pad_glyph(d,items[i].button,ImVec2(x+icon*.5f,y+fs*.52f),k);
  d->AddText(f,fs,ImVec2(x+icon+9*k,y+k),IM_COL32(0,0,0,210),items[i].label);
  d->AddText(f,fs,ImVec2(x+icon+8*k,y),IM_COL32(243,236,239,255),items[i].label);x+=w;
 }return y-pos.y+h;
}
static void mod_pad_hint_items(const ModPadHint*items,int n,float k=1,float fs=16){ImVec2 p=ImGui::GetCursorScreenPos();float h=mod_pad_hints(ImGui::GetWindowDrawList(),p,ImGui::GetContentRegionAvail().x,k,items,n,fs);ImGui::Dummy(ImVec2(0,h));}
