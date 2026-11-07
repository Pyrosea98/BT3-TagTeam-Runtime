#ifndef MOD_STORY_GUIDE_H
#define MOD_STORY_GUIDE_H
/* Render-thread guide speech only: no game state, allocation, I/O or audio playback. */
#define STG_IDLE_LINES 3
#define STG_FIRST_PAUSE 14.0
#define STG_REPEAT_PAUSE 18.0
typedef struct StGuideTalk {
 int key,activity,lastFrame,line,nextLine,initialized;
 double lastSeen,until,nextAt;
} StGuideTalk;
static double STG_Duration(const char *s){
 int n=0;if(s)for(const unsigned char*p=(const unsigned char*)s;*p;p++)if((*p&192)!=128)n++;
 double seconds=1.4+n*.055;return seconds<2.5?2.5:seconds>7.0?7.0:seconds;
}
/* A missing render frame means this menu was left/covered. Reopen with its greeting,
   even when the same page is selected again. A stalled frame starts at most ONE idle line. */
static int STG_Advance(StGuideTalk *s,double now,int frame,int key,int activity,
                       const char *opening,const char *const idle[STG_IDLE_LINES]){
 if(!s->initialized||key!=s->key||frame!=(long long)s->lastFrame+1||now<s->lastSeen){
  s->initialized=1;s->key=key;s->activity=activity;s->line=-1;s->nextLine=0;
  s->until=now+STG_Duration(opening);s->nextAt=s->until+STG_FIRST_PAUSE;
 }else if(activity!=s->activity){
  s->activity=activity;
  if(s->line>=0){s->line=-1;s->until=now;}
  s->nextAt=(s->until>now?s->until:now)+STG_FIRST_PAUSE;
 }
 if(now>=s->nextAt&&now>=s->until){
  s->line=s->nextLine++%STG_IDLE_LINES;
  s->until=now+STG_Duration(idle[s->line]);s->nextAt=s->until+STG_REPEAT_PAUSE;
 }
 s->lastFrame=frame;s->lastSeen=now;return now<s->until;
}
/* [guide][language EN/ES][idle variation]. Original written comments, no new recorded voices. */
static const char *const stGuideIdle[9][2][STG_IDLE_LINES]={
 {{"Take your time! Choosing the next adventure is part of the fun.","Do you think we can find something to eat after the fight?","A strong opponent always gets me excited! Ready when you are."},{"\302\241T\303\263mate tu tiempo! Elegir la pr\303\263xima aventura tambi\303\251n tiene su gracia.","\302\277Crees que despu\303\251s de la pelea encontraremos algo para comer?","\302\241Un rival fuerte siempre me emociona! Cuando quieras, empezamos."}},
 {{"Study your opponent. There is always a weak point.","Stay focused. The next move can decide the battle.","Do not rush. Think, then act."},{"Estudia a tu rival. Siempre hay un punto d\303\251bil.","Mant\303\251n la concentraci\303\263n. El pr\303\263ximo movimiento puede decidir la pelea.","No te precipites. Piensa y despu\303\251s act\303\272a."}},
 {{"Still deciding? Do not keep a Saiyan prince waiting.","A real warrior never backs down from a challenge.","Power means nothing if you cannot control it."},{"\302\277Todav\303\255a est\303\241s eligiendo? No hagas esperar al pr\303\255ncipe de los saiyajin.","Un verdadero guerrero no retrocede ante un desaf\303\255o.","El poder no sirve de nada si no sabes controlarlo."}},
 {{"Every choice can change the future.","We cannot change the past, but we can learn from it.","When you are ready, we will face this together."},{"Cada decisi\303\263n puede cambiar el futuro.","No podemos cambiar el pasado, pero podemos aprender de \303\251l.","Cuando est\303\251s listo, afrontaremos esto juntos."}},
 {{"With the great Mr. Satan here, you have nothing to fear!","A champion knows when to make a spectacular entrance!","No need to rush! Even a hero needs a moment to prepare."},{"\302\241Con el gran Mr. Satan aqu\303\255, no tienes nada que temer!","\302\241Un campe\303\263n sabe cu\303\241ndo hacer una entrada espectacular!","\302\241Sin prisas! Hasta un h\303\251roe necesita prepararse un momento."}},
 {{"Strength is not everything. You also need to think on your feet.","Take a breath. We can handle the next challenge.","Ready to give it another try? I am right here."},{"La fuerza no lo es todo. Tambi\303\251n hay que saber reaccionar.","Respira. Podemos superar el pr\303\263ximo desaf\303\255o.","\302\277Listo para intentarlo otra vez? Aqu\303\255 estoy."}},
 {{"Come on! The adventure is not going to wait forever!","I bet we can go even further this time!","Which adventure is next? I cannot wait!"},{"\302\241Vamos! \302\241La aventura no va a esperarnos para siempre!","\302\241Seguro que esta vez llegamos todav\303\255a m\303\241s lejos!","\302\277Cu\303\241l ser\303\241 la pr\303\263xima aventura? \302\241Ya quiero empezar!"}},
 {{"Training is important, but so is a good meal!","Maybe we will find someone really strong on this adventure!","I wonder what is waiting for us out there."},{"\302\241Entrenar es importante, pero comer bien tambi\303\251n!","\302\241Quiz\303\241 encontremos a alguien muy fuerte en esta aventura!","Me pregunto qu\303\251 nos estar\303\241 esperando ah\303\255 fuera."}},
 {{"Take your time. Many lives depend on what comes next.","Even the greatest danger can be overcome with courage.","When you are ready, we shall continue."},{"T\303\263mate tu tiempo. Muchas vidas dependen de lo que viene.","Hasta el mayor peligro puede superarse con valent\303\255a.","Cuando est\303\251s preparado, continuaremos."}},
};
#endif
