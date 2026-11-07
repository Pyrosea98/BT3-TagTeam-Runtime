extern "C" int Port_StoryInterludeLoading(void);
#include "mod_story_prefight.inc"
// Subtitles of the Dragon History scenes (port/include/mod_story_scene.inc): the speaker and the line in the
// menu language, at the bottom of the picture. Reads only the immutable chapter tables and the packed view the
// game thread publishes (Port_StorySubtitleView).
#include "../../include/mod_story.h"
static bool mod_story_subtitle_visible(){return Port_StoryInterludeLoading()||Port_StoryPromptView()||Port_StoryPresentationView(nullptr,nullptr,nullptr)||Port_StorySubtitleView(nullptr,nullptr,nullptr)!=0;}
static void mod_story_subtitle_draw(){
 if(Port_StoryInterludeLoading()){ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(0,0),ImGui::GetIO().DisplaySize,IM_COL32(0,0,0,255));return;}
 if(Port_StoryPromptView()&&gUiPresentW>0&&gUiPresentH>0){
  ImDrawList*d=ImGui::GetForegroundDrawList();float k=gUiPresentH/720.f;
  ImVec2 center(gUiPresentX+gUiPresentW*.5f,gUiPresentY+gUiPresentH*.80f);
  d->AddCircle(center,25*k,IM_COL32(255,216,106,185),40,1.6f*k);
  mod_pad_glyph(d,MP_R3,center,1.65f*k);
 }

 int presentationChapter,presentationPhase,presentationProgress;if(Port_StoryPresentationView(&presentationChapter,&presentationPhase,&presentationProgress)){mod_story_prefight_draw(presentationChapter,presentationPhase,presentationProgress);return;}
 int chapter=0,set=0,line=0;if(!Port_StorySubtitleView(&chapter,&set,&line)||gUiPresentW<=0||gUiPresentH<=0)return;
 int L=Port_Setting("mod_language",1)==1?1:0;const char*speaker="",*text=nullptr;
 if(set==ST_SET_LEAVES||set==ST_SET_LEAVES_ALT)text=ST_LeavesText(chapter,set,L);
 else if(const StLine*l=ST_SceneLine(chapter,set,line)){speaker=ST_SpeakerName(l->speaker,L);text=l->text[L];}
 if(!text||!*text)return;
 ImFont*f=sStoryReadableFont?sStoryReadableFont:sModBodyFont?sModBodyFont:ImGui::GetFont();
 ImDrawList*d=ImGui::GetForegroundDrawList();
 float k=gUiPresentH/720.f,w=SDL_min(gUiPresentW*.82f,1040*k),fs=22*k,ns=15*k;
 ImVec2 size=f->CalcTextSizeA(fs,10000,w,text);
 // Subtitle-sized, content-width composition. No opaque card over the fight.
 float block=SDL_min(w,SDL_max(size.x,140*k));
 float x=gUiPresentX+(gUiPresentW-block)*.5f;
 float h=size.y+(*speaker?ns+8*k:0),y=gUiPresentY+gUiPresentH-h-38*k;
 float ty=y;
 if(*speaker){
  ImU32 col=ST_LineSide(chapter,ST_SceneLine(chapter,set,line))?IM_COL32(255,155,129,255):IM_COL32(128,201,255,255);
  d->AddLine(ImVec2(x,ty+ns*.55f),ImVec2(x+12*k,ty+ns*.55f),IM_COL32(255,215,100,185),1.3f*k);
  d->AddText(f,ns,ImVec2(x+19*k,ty+1*k),IM_COL32(0,0,0,225),speaker);
  d->AddText(f,ns,ImVec2(x+18*k,ty),col,speaker);ty+=ns+8*k;
 }
 // A thin contour stays legible on sky, grass and bright attacks without a plate.
 for(int i=0;i<4;i++){float ox=i==0?-k:i==1?k:0,oy=i==2?-k:i==3?k:0;
  d->AddText(f,fs,ImVec2(x+ox,ty+oy),IM_COL32(0,0,0,220),text,nullptr,w);}
 d->AddText(f,fs,ImVec2(x,ty),(set==ST_SET_LEAVES||set==ST_SET_LEAVES_ALT)?IM_COL32(224,219,228,255):IM_COL32(249,245,237,255),text,nullptr,w);
}
