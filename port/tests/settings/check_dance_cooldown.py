from pathlib import Path
import tempfile,subprocess
r=Path.cwd();s=(r/'port/src/gs/ui.cpp').read_text();a=s.index('static void mod_fusion_settings(){');b=s.index('static void mod_',a+20);opening=s.index('{',a);i=opening+1;depth=1
while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
body=s[a:i]
h=r'''#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <cstdio>
#include "imgui.h"
#include "imgui_internal.h"
static std::map<std::string,int> values,saved;static int writes;static ImGuiID beamId,ultimateId,fixedId;static ImGuiWindow*window;
int Port_Setting(const char*k,int d){auto i=values.find(k);return i==values.end()?d:i->second;}void Port_SettingSave(const char*k,int v){values[k]=v;}int Port_SettingsWrite(){saved=values;writes++;return 1;}
#include "mod_settings_l10n.inc"
'''
f=r'''void frame(){ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(1200,3000));ImGui::Begin("fusion");mod_fusion_settings();fixedId=ImGui::GetID(mod_l10n("Dance fusion cooldown"));ImGui::End();ImGui::Render();}
int main(){ImGui::CreateContext();auto&io=ImGui::GetIO();io.DisplaySize=ImVec2(1400,3400);io.DeltaTime=1.f/60;io.IniFilename=nullptr;io.ConfigFlags|=ImGuiConfigFlags_NavEnableGamepad;io.BackendFlags|=ImGuiBackendFlags_HasGamepad;unsigned char*px;int w,h;io.Fonts->GetTexDataAsRGBA32(&px,&w,&h);
for(int lang=0;lang<2;lang++){values.clear();values["mod_language"]=lang;frame();assert(Port_Setting("fusion_dance_cooldown_seconds",10)==10);
ImGui::NewFrame();ImGui::Begin("fusion");mod_fusion_settings();ImGui::FocusWindow(ImGui::GetCurrentWindow());ImGui::SetNavID(fixedId,ImGuiNavLayer_Main,ImGui::GetCurrentWindow()->NavRootFocusScopeId,ImRect(0,0,100,20));ImGui::SetNavCursorVisible(true);ImGui::End();ImGui::Render();
io.AddKeyEvent(ImGuiKey_GamepadFaceDown,true);frame();io.AddKeyEvent(ImGuiKey_GamepadFaceDown,false);frame();io.AddKeyEvent(ImGuiKey_GamepadDpadRight,true);for(int n=0;n<12;n++)frame();io.AddKeyEvent(ImGuiKey_GamepadDpadRight,false);frame();assert(Port_Setting("fusion_dance_cooldown_seconds",10)>10&&writes>0);values=saved;assert(values["fusion_dance_cooldown_seconds"]>10);}
puts("PASS actual Fusion settings: default10seconds, real gamepad slider edit+saved key, English/Spanish");ImGui::DestroyContext();}
'''
with tempfile.TemporaryDirectory()as d:
 p=Path(d);(p/'t.cpp').write_text(h+body+f);img=r/'port/third_party/imgui';subprocess.run(['g++','-std=c++17','-O1','-I'+str(img),'-I'+str(r/'port/src/gs'),str(p/'t.cpp')]+[str(img/n)for n in ['imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp']]+['-o',str(p/'t')],check=True);subprocess.run([str(p/'t')],check=True)
