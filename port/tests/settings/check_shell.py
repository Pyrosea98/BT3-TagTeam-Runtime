import subprocess,tempfile
from pathlib import Path
r=Path.cwd();s=(r/'port/src/gs/ui.cpp').read_text();a=s.index('static void build(void) {');b=s.index('\n#include "mod_menu_ui.inc"',a);body=s[s.index('static void mod_giant_settings(){'):s.index('static void mod_training_settings(){')]+s[a:b]
head=r"""#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include "imgui.h"
#include "imgui_internal.h"
#define SDL_min std::min
#define SDL_max std::max
static ImFont *sModBodyFont;
struct SDL_GPUTexture{};struct PortVideo{int scale=1;};
static bool editValue;static int sliderValue=50;static ImGuiID editId,sliderId;static int sCapKind,sCapUntil,gPortInputCapture,sForceTab=-1,sTab,sPadTab,press=-1,calls[12],flushes,flushResult=1;
int SDL_GetTicks(){return 0;}int pad_pressed(ImGuiKey key){return key==press||ImGui::IsKeyPressed(key,false);}
int Port_Setting(const char*,int d){return d;}void GsGpu_GetSettings(PortVideo *v){v->scale=1;}void GsGpu_SetSettings(PortVideo*){}
const char *Port_FileRoot(){return "/missing";}SDL_GPUTexture *Ui_Texture(unsigned char *p,unsigned,unsigned){free(p);return nullptr;}
SDL_GPUTexture *mod_brand_texture(){return nullptr;}int Port_SettingsFlush(){flushes++;return flushResult;}void Ui_QuitAsk(){}void set_open(bool){}
void video_tab(PortVideo&){calls[0]++;ImGui::Checkbox("Display setting",&editValue);editId=ImGui::GetItemID();ImGui::SliderInt("Display value",&sliderValue,0,100);sliderId=ImGui::GetItemID();}
void effects_tab(PortVideo&){calls[1]++;}void audio_tab(PortVideo&){calls[2]++;}void ui_tab(PortVideo&){calls[3]++;}
void mod_controls_settings(){calls[4]++;}void cheats_tab(){calls[5]++;}void misc_tab(){calls[6]++;}void experiments_tab(){calls[7]++;}void mod_hud_settings(){calls[8]++;}void mod_fusion_settings(){calls[9]++;}void mod_training_settings(){calls[10]++;}void mod_struggle_settings(){calls[11]++;}
void Port_SettingSave(const char*,int){}void Port_SettingsWrite(){}const char*mod_l10n(const char*s){return s;}
#include "mod_menu_icons.inc"
"""
foot=r"""
int main(){ImGui::CreateContext();auto &io=ImGui::GetIO();io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;io.BackendFlags|=ImGuiBackendFlags_HasGamepad;io.IniFilename=nullptr;io.Fonts->AddFontDefault();unsigned char *pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);io.Fonts->SetTexID(1);
 int widths[]={640,1440,1920,2520},heights[]={480,1080,1080,1080},frames=0;
 for(int shape=0;shape<4;shape++)for(int tab=0;tab<12;tab++){
  io.DisplaySize=ImVec2(widths[shape],heights[shape]);io.DeltaTime=1.f/60;sForceTab=tab;
  ImGui::NewFrame();build();assert(sTab==tab&&sForceTab==-1);ImGui::Render();frames++;
  auto *dd=ImGui::GetDrawData();assert(dd&&dd->TotalVtxCount>0);for(int l=0;l<dd->CmdListsCount;l++)for(auto &v:dd->CmdLists[l]->VtxBuffer){assert(std::isfinite(v.pos.x)&&std::isfinite(v.pos.y));}
 }
 for(int n:calls)assert(n>=4);
 press=ImGuiKey_GamepadFaceLeft;ImGui::NewFrame();build();ImGui::Render();assert(flushes==1);
 sCapKind=1;ImGui::NewFrame();build();ImGui::Render();assert(flushes==1);sCapKind=0;flushResult=0;
 ImGui::NewFrame();build();ImGui::Render();assert(flushes==2);press=-1;
 // Start on the category, then enter options via the real d-pad handoff (no direct widget focus).
 press=-1;sForceTab=0;ImGui::NewFrame();build();ImGuiContext &nav=*ImGui::GetCurrentContext();ImGuiWindow *category=nullptr;
 for(ImGuiWindow *w:nav.Windows)if(strstr(w->Name,"/categories_"))category=w;
 assert(category);ImGui::FocusWindow(category);ImGui::SetNavID(category->GetID("Video"),ImGuiNavLayer_Main,category->NavRootFocusScopeId,ImRect(0,0,100,30));ImGui::SetNavCursorVisible(true);ImGui::Render();
 io.AddKeyEvent(ImGuiKey_GamepadDpadRight,true);ImGui::NewFrame();build();ImGui::Render();
 io.AddKeyEvent(ImGuiKey_GamepadDpadRight,false);for(int n=0;n<3;n++){ImGui::NewFrame();build();ImGui::Render();}
 assert(nav.NavId==editId);assert(strstr(nav.NavWindow->Name,"/options_"));
 io.AddKeyEvent(ImGuiKey_GamepadFaceDown,true);ImGui::NewFrame();build();ImGui::Render();io.AddKeyEvent(ImGuiKey_GamepadFaceDown,false);ImGui::NewFrame();build();ImGui::Render();assert(editValue);
 io.AddKeyEvent(ImGuiKey_GamepadDpadLeft,true);ImGui::NewFrame();build();ImGui::Render();io.AddKeyEvent(ImGuiKey_GamepadDpadLeft,false);ImGui::NewFrame();build();ImGui::Render();assert(strstr(nav.NavWindow->Name,"/categories_"));
 editValue=false;
 // Real ImGui gamepad activation edits a child-panel option after navigation focus.
 press=-1;sForceTab=0;ImGui::NewFrame();build();ImGui::Render();assert(editId);
 ImGui::NewFrame();build();ImGuiWindow *options=ImGui::FindWindowByName("Mod Settings###Settings/options_" );(void)options;
 ImGuiContext &g=*ImGui::GetCurrentContext();ImGuiWindow *editWindow=g.NavWindow;
 // Resolve the actual options child rather than the category list.
 for(ImGuiWindow *window:g.Windows)if(strstr(window->Name,"/options_"))editWindow=window;
 assert(editWindow);ImGui::FocusWindow(editWindow);ImGui::SetNavID(editId,ImGuiNavLayer_Main,editWindow->NavRootFocusScopeId,ImRect(0,0,20,20));g.NavCursorVisible=true;g.NavHighlightItemUnderNav=true;ImGui::Render();
 io.AddKeyEvent(ImGuiKey_GamepadFaceDown,true);ImGui::NewFrame();build();ImGui::Render();
 io.AddKeyEvent(ImGuiKey_GamepadFaceDown,false);ImGui::NewFrame();build();ImGui::Render();fprintf(stderr,"nav id=%u edit=%u window=%s value=%d active=%d source=%d key=%d\n",g.NavId,editId,g.NavWindow?g.NavWindow->Name:"none",editValue,io.NavActive,g.NavInputSource,ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown));assert(editValue);
 // Real gamepad left/right also changes a focused numeric option.
 ImGui::NewFrame();build();ImGui::FocusWindow(editWindow);ImGui::SetNavID(sliderId,ImGuiNavLayer_Main,editWindow->NavRootFocusScopeId,ImRect(0,0,20,20));g.NavCursorVisible=true;g.NavHighlightItemUnderNav=true;ImGui::Render();
 io.AddKeyEvent(ImGuiKey_GamepadFaceDown,true);ImGui::NewFrame();build();ImGui::Render();io.AddKeyEvent(ImGuiKey_GamepadFaceDown,false);ImGui::NewFrame();build();ImGui::Render();
 io.AddKeyEvent(ImGuiKey_GamepadDpadRight,true);for(int n=0;n<12;n++){ImGui::NewFrame();build();ImGui::Render();}
 io.AddKeyEvent(ImGuiKey_GamepadDpadRight,false);ImGui::NewFrame();build();ImGui::Render();assert(sliderValue>50);
 // Every semantic icon generates finite uncropped geometry inside its own allocation.
 ImGui::NewFrame();auto *d=ImGui::GetForegroundDrawList();for(int icon=0;icon<=MI_STRUGGLES;icon++){int start=d->VtxBuffer.Size;mod_icon_draw(d,icon,ImVec2(100,100),20,IM_COL32_WHITE);assert(d->VtxBuffer.Size>start);for(int n=start;n<d->VtxBuffer.Size;n++){auto p=d->VtxBuffer[n].pos;if(!(p.x>=77&&p.x<=123&&p.y>=77&&p.y<=123)){fprintf(stderr,"icon %d outside: %f %f\n",icon,p.x,p.y);abort();}}}ImGui::Render();
 printf("PASS actual unified settings shell: %d layout/category cases, all consumers dispatched, Square save/capture guard,20 bounded semantic icons\n",frames);ImGui::DestroyContext();}
"""
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);f=p/'test.cpp';f.write_text(head+body+foot);img=r/'port/third_party/imgui';out=p/'test'
 subprocess.run(['g++','-std=c++17','-O1','-I'+str(img),'-I'+str(r/'port/src/gs'),str(f)]+[str(img/n) for n in ['imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp']]+['-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
