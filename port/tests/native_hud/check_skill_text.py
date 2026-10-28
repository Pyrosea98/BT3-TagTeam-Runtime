from pathlib import Path
import subprocess
import tempfile


ROOT = Path.cwd()


def extract_function(text, name, return_type):
    start = text.index(f"{return_type} {name}(")
    opening = text.index("{", start) + 1
    depth = 1
    cursor = opening
    while depth:
        depth += (text[cursor] == "{") - (text[cursor] == "}")
        cursor += 1
    return text[start:cursor]


def optional_function(text, name, return_type):
    try:
        return extract_function(text, name, return_type) + "\n"
    except ValueError:
        return ""


source = (ROOT / "port/src/plat_fighters.c").read_text()
parts = [extract_function(source, "skill_name_units", "static int")]
for helper in ("skill_has", "skill_partners", "skill_emit_entry"):
    parts.append(optional_function(source, helper, "static int"))
parts.append(extract_function(source, "Port_PatchSkillText", "void"))

harness = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* These fixtures use unmodified Goku3; split-form policy is covered by the native module integration test. */
int Port_Beta8PatchFormParam(int c,void *p){assert(c==3||c==250);(void)p;return 0;}
int Port_Beta8FormTarget(int c,int t){assert(c==3||c==250);return t;}
static unsigned char text_arena[65536];
unsigned Port_GameBigAlloc(int size) {
    assert(size >= 0 && (unsigned)size <= sizeof(text_arena));
    return (unsigned)(uintptr_t)text_arena;
}
const char *Port_CharaName(int chara) {
    switch (chara) {
        case 21: return "Gohan";
        case 34: return "Vegeta";
        case 51: return "Vegetto";
        case 54: case 110: return "Gogeta";
        case 214: return "Gokhan";
        default: return "Unknown";
    }
}
"""

footer = r"""
static int contains(const uint16_t *text, const uint16_t *needle) {
    int i = 0;
    while (needle[i]) {
        if (text[i] != needle[i]) return 0;
        i++;
    }
    return 1;
}

int main(void) {
    unsigned char param[256];memset(param,255,sizeof(param));
    static uint16_t original[] = {
        '!','F','*','0','0','4','F','u','s','i','o','n','[','V','e','g','e','t','t','o',']','\n',
        '!','F','%','0','P','a','r','e','j','a',':',' ','V','e','g','e','t','a','\n',
        '!','F','*','0','0','5','D','a','n','s','a','[','G','o','g','e','t','a',']','\n',
        '!','F','%','0','P','a','r','e','j','a',':',' ','V','e','g','e','t','a','\n',0
    };
    uint16_t snapshot[sizeof(original) / sizeof(original[0])];
    uint16_t expected_gokhan[] = {'V','e','g','e','t','t','o',0};
    uint16_t expected_partners[] = {'G','o','h','a','n',' ','/',' ','V','e','g','e','t','a',0};
    uint16_t expected_gogeta[] = {'G','o','g','e','t','a',0};
    uint16_t expected_vegeta[] = {'V','e','g','e','t','a',0};
    uint16_t *slot = original;

    memcpy(snapshot, original, sizeof(original));
    param[0xAE + 0] = 1;
    param[0xB1 + 0] = 2;
    param[0xB4 + 0] = 51;
    param[0xBA + 0] = 21;
    param[0xBA + 1] = 34;
    param[0xBA + 2] = 21;
    param[0xBA + 3] = 255;
    param[0xAE + 1] = 0;
    param[0xB1 + 1] = 1;
    param[0xB4 + 1] = 54;
    param[0xBA + 4] = 34;

    uint32_t fields[2] = {(uint32_t)(uintptr_t)slot, 0xDEADBEEFu};
    assert((uintptr_t)slot <= UINT32_MAX);
    Port_PatchSkillText(250, 0, param, fields);
    assert(fields[1] == 0xDEADBEEFu); /* adjacent game field untouched */
    slot = (uint16_t *)(uintptr_t)fields[0];
    assert(slot != original); /* the model's source text is never edited */
    assert(memcmp(original, snapshot, sizeof(original)) == 0);

    const uint16_t *lines[5] = {slot, NULL, NULL, NULL, NULL};
    int line_count = 1;
    for (int i = 0; slot[i] && line_count < 5; i++) {
        if (slot[i] == '\n') lines[line_count++] = slot + i + 1;
    }
    assert(line_count == 5 && lines[4][0] == 0);

    /* Only the third digit is the stock cost; the two icon digits survive. */
    assert(lines[0][3] == '0' && lines[0][4] == '0' && lines[0][5] == '1');
    assert(contains(lines[0] + 13, expected_gokhan)); /* !F*004Fusion[ */
    assert(contains(lines[1] + 12, expected_partners));  /* !F%0Pareja: , deduplicated in recipe order */
    assert(lines[2][3] == '0' && lines[2][4] == '0' && lines[2][5] == '0');
    assert(contains(lines[2] + 12, expected_gogeta)); /* !F*005Dansa[ */
    assert(contains(lines[3] + 12, expected_vegeta));

    /* Canonical Goku costume3 retains Gokhan beside both Fin pair styles. */
    memset(param,255,sizeof(param));fields[0]=(uint32_t)(uintptr_t)original;
    Port_PatchSkillText(3,2,param,fields);slot=(uint16_t*)(uintptr_t)fields[0];
    uint16_t custom_name[]={'G','o','k','h','a','n',0};
    int found_custom=0;for(int n=0;slot[n];n++)if(contains(slot+n,custom_name))found_custom=1;
    assert(found_custom&&param[0xb6]==214&&param[0xb9]==21&&fields[1]==0xDEADBEEFu);
    puts("PASS skill text: live costs update only digit three, all unique partners listed in recipe order, names/source preserved");
    return 0;
}
"""

with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    source_path = work / "test.cpp"
    binary = work / "test"
    source_path.write_text(harness + "\n".join(parts) + "\n" + footer)
    subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer",
            "-no-pie",
            str(source_path),
            "-o",
            str(binary),
        ],
        check=True,
    )
    subprocess.run([str(binary)], check=True)
