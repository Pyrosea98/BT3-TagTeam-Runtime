from pathlib import Path
import tempfile,subprocess
r=Path.cwd();s=(r/'port/src/gs/ui.cpp').read_text();a=s.index('static void mod_struggle_settings(){');b=s.index('// F1 and Select',a);body=s[a:b]
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
f=r'''void frame(){ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImVec2(1200,3000));ImGui::Begin("assist");window=ImGui::GetCurrentWindow();mod_struggle_settings();ImGui::PushID(0);beamId=ImGui::GetID(mod_l10n("Damage scaling"));fixedId=ImGui::GetID(mod_l10n("Fixed damage multiplier"));ImGui::PopID();ImGui::PushID(2);ultimateId=ImGui::GetID(mod_l10n("Damage scaling"));ImGui::PopID();ImGui::End();ImGui::Render();}
void key(ImGuiKey k){auto&io=ImGui::GetIO();io.AddKeyEvent(k,true);frame();io.AddKeyEvent(k,false);frame();}
void focus(ImGuiID id){ImGui::NewFrame();ImGui::Begin("assist");mod_struggle_settings();ImGui::FocusWindow(ImGui::GetCurrentWindow());ImGui::SetNavID(id,ImGuiNavLayer_Main,ImGui::GetCurrentWindow()->NavRootFocusScopeId,ImRect(0,0,100,20));ImGui::SetNavCursorVisible(true);ImGui::End();ImGui::Render();}
int main(){ImGui::CreateContext();auto&io=ImGui::GetIO();io.DisplaySize=ImVec2(1400,3400);io.DeltaTime=1.f/60;io.IniFilename=nullptr;io.ConfigFlags|=ImGuiConfigFlags_NavEnableGamepad;io.BackendFlags|=ImGuiBackendFlags_HasGamepad;unsigned char*px;int w,h;io.Fonts->GetTexDataAsRGBA32(&px,&w,&h);
 for(int lang=0;lang<2;lang++){
  values.clear();values["mod_language"]=lang;writes=0;frame();focus(beamId);key(ImGuiKey_GamepadFaceDown);key(ImGuiKey_GamepadDpadDown);key(ImGuiKey_GamepadFaceDown);assert(values["beam_assist_damage_mode"]==1&&writes>0);
  frame();focus(ultimateId);key(ImGuiKey_GamepadFaceDown);key(ImGuiKey_GamepadDpadDown);key(ImGuiKey_GamepadFaceDown);assert(values["ultimate_boost_damage_mode"]==1);
  values["beam_assist_damage_mode"]=2;values["beam_assist_damage_fixed_percent"]=200;frame();focus(fixedId);key(ImGuiKey_GamepadFaceDown);io.AddKeyEvent(ImGuiKey_GamepadDpadRight,true);for(int n=0;n<12;n++)frame();io.AddKeyEvent(ImGuiKey_GamepadDpadRight,false);frame();assert(values["beam_assist_damage_fixed_percent"]>200);values=saved;assert(values["ultimate_boost_damage_mode"]==1&&values["beam_assist_damage_mode"]==2);
 }
 puts("PASS actual assist settings +real ImGui gamepad: beam/ultimate mode changes, fixed multiplier adjustment, saved keys and English/Spanish labels");ImGui::DestroyContext();}
'''
with tempfile.TemporaryDirectory()as d:
 p=Path(d);(p/'t.cpp').write_text(h+body+f);img=r/'port/third_party/imgui';subprocess.run(['g++','-std=c++17','-O1','-I'+str(img),'-I'+str(r/'port/src/gs'),str(p/'t.cpp')]+[str(img/n)for n in ['imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp']]+['-o',str(p/'t')],check=True);subprocess.run([str(p/'t')],check=True)
