#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
extern int Port_Setting(const char *, int);
extern void Port_SettingSave(const char *, int);
extern void Port_SettingText(const char *, const char *, char *, int);
extern void Port_SettingTextSave(const char *, const char *);
extern void Port_SettingsWrite(void);
extern int Port_SettingsFlush(void);
int main(int argc, char **argv) {
    char key[96], text[64];
    assert(argc == 2);
    if (!strcmp(argv[1], "write")) {
        for (int i=0; i<400; ++i) {
            snprintf(key, sizeof(key), "migration_option_with_long_name_%03d", i);
            Port_SettingSave(key,i+17);
        }
        Port_SettingSave("beam_struggle_damage_penalty_percent", 200);
        Port_SettingTextSave("migration_long_text_setting_name", "Juan's mod");
        memset(key,'x',sizeof(key)-1); key[sizeof(key)-1]=0;
        Port_SettingSave(key,999); /* must not turn into a different 63-byte key */
        Port_SettingSave("bad=key", 999);
        assert(Port_SettingsFlush()==1);
        assert(Port_SettingsFlush()==1); /* explicit save validates disk even with no dirty keys */
    } else if (!strcmp(argv[1], "read")) {
        for (int i=0; i<400; ++i) {
            snprintf(key,sizeof(key),"migration_option_with_long_name_%03d",i);
            assert(Port_Setting(key,-1)==i+17);
        }
        assert(Port_Setting("beam_struggle_damage_penalty_percent",-1)==200);
        Port_SettingText("migration_long_text_setting_name", "", text, sizeof(text));
        assert(!strcmp(text,"Juan's mod"));
        memset(key,'x',63); key[63]=0;
        assert(Port_Setting(key,-1)==-1);
    } else if (!strcmp(argv[1], "retry")) {
        Port_SettingSave("pending_save",73);
        assert(Port_SettingsFlush()==0); /* never report success for inaccessible destination */
        assert(mkdir("missing",0700)==0);
        assert(Port_SettingsFlush()==1); /* successful retry */
        FILE *f=fopen(getenv("BT3_SETTINGS"),"r"); assert(f);
        assert(fgets(key,sizeof(key),f)); assert(!strcmp(key,"pending_save=73\n")); fclose(f);
    } else if (!strcmp(argv[1], "legacy")) {
        assert(Port_Setting("scale",0)==2);
        assert(Port_Setting("clash_square",0)==1);
        Port_SettingSave("scale",4); Port_SettingsWrite();
    } else if (!strcmp(argv[1], "full")) {
        (void)Port_Setting("init",0);assert(setenv("BT3_SETTINGS","/dev/full",1)==0);
        Port_SettingSave("pending_save",73);assert(Port_SettingsFlush()==0);
    } else if (!strcmp(argv[1], "fail")) {
        Port_SettingSave("pending_save",73);assert(Port_SettingsFlush()==0);
    } else return 2;
    return 0;
}
