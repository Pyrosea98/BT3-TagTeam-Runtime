import subprocess
subprocess.run(["python3","port/tests/native_hud/check_defuse_cinematic.py"],check=True)
import subprocess
subprocess.run(["python3","port/tests/native_hud/check_defusion_burst.py"],check=True)
import subprocess
subprocess.run(["python3","port/tests/native_hud/check_fusion_collision.py"],check=True)
import subprocess
subprocess.run(["python3","port/tests/native_hud/check_vegetto59_art.py"],check=True)
import subprocess
subprocess.run(["python3","port/tests/native_hud/check_giants.py"],check=True)
import subprocess
subprocess.run(["python3","port/tests/native_hud/check_round6.py"],check=True)
import subprocess
import subprocess
subprocess.run(["python3","port/tests/native_hud/check_menu_loading.py"],check=True)
import subprocess
subprocess.run(["python3","port/tests/native_hud/check_ssj4_lightning.py"],check=True)
import subprocess
subprocess.run(["python3","port/tests/native_hud/check_initial_fusion.py"],check=True)
import subprocess
subprocess.run(["python3","port/tests/native_hud/check_charge_followup.py"],check=True)
import subprocess
subprocess.run(["python3","port/tests/native_hud/check_round3_runtime.py"],check=True)
import subprocess
subprocess.run(["python3","port/tests/native_hud/check_fusion_lifecycle.py"],check=True)
import subprocess
subprocess.run(["python3","port/tests/native_hud/check_assist_formation.py"],check=True)
import subprocess
subprocess.run(["python3","port/tests/native_hud/check_revive_ring.py"],check=True)
import subprocess
subprocess.run(["python3","port/tests/native_hud/check_training_health.py"],check=True)
import subprocess
subprocess.run(["python3","port/tests/native_hud/check_native_takeover.py"],check=True)
import subprocess
subprocess.run(["python3","port/tests/native_hud/check_native_assists.py"],check=True)
subprocess.run(["python3","port/tests/native_hud/check_assist_boundaries.py"],check=True)
import subprocess
subprocess.run(["python3","port/tests/native_hud/check_assist_outcome.py"],check=True)
import subprocess
subprocess.run(["python3","port/tests/native_hud/check_cpu_tactics.py"],check=True)
import subprocess
subprocess.run(["python3","port/tests/native_hud/check_tag_switch.py"],check=True)
import subprocess
subprocess.run(["python3","port/tests/native_hud/check_gogeta_chain.py"],check=True)
import subprocess
subprocess.run(["python3","port/tests/native_hud/check_native_revive.py"],check=True)
subprocess.run(["python3","port/tests/native_hud/check_fusion_partner_hud.py"],check=True)
subprocess.run(["python3","port/tests/native_hud/check_fusion_admission.py"],check=True)
subprocess.run(["python3","port/tests/native_hud/check_fusion_attach_failure.py"],check=True)
subprocess.run(["python3","port/tests/native_hud/check_skill_text.py"],check=True)
subprocess.run(["python3","port/tests/native_hud/check_skill_links.py"],check=True)
subprocess.run(["python3","port/tests/native_hud/check_faceless_model.py"],check=True)
import subprocess
subprocess.run(["python3","port/tests/native_hud/check_native_counters.py"],check=True)
subprocess.run(["python3","port/tests/native_hud/check_cinematic_projection.py"],check=True)
import subprocess,sys,tempfile
from pathlib import Path
r=Path.cwd();p=r/'port/tests/native_hud'
for name in ['check-native-fusion-clock.py','check-partner-restore.py','check_gameplay.py']:
 subprocess.run([sys.executable,str(p/name)],check=True)
with tempfile.TemporaryDirectory() as tmp:
 img=r/'port/third_party/imgui';out=Path(tmp)/'hud'
 subprocess.run(['g++','-std=c++17','-O1','-I'+str(img),'-I'+str(r/'port/src/gs'),str(p/'check-hud-renderer.cpp')]+[str(img/n) for n in ['imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp']]+['-o',str(out)],check=True)
 subprocess.run([str(out)]+sys.argv[1:],check=True)

subprocess.run(["python3","port/tests/native_hud/check_assist_pose.py"],check=True)
subprocess.run(["python3","port/tests/native_hud/check_model_buffer_fit.py"],check=True)

subprocess.run(["python3","port/tests/native_hud/check_target_lock.py"],check=True)

subprocess.run(["python3","port/tests/native_hud/check_transform_rematch.py"],check=True)
subprocess.run(["python3","port/tests/native_hud/check_form_focus.py"],check=True)

subprocess.run(["python3","port/tests/native_hud/check_cinematic_ai_pair.py"],check=True)

subprocess.run(["python3","port/tests/native_hud/check_active_team_result.py"],check=True)

subprocess.run(["python3","port/tests/native_hud/check_charged_qte.py"],check=True)

subprocess.run(["python3","port/tests/native_hud/check_beam_audio.py"],check=True)
subprocess.run(["python3","port/tests/native_hud/check_qte_assist.py"],check=True)
