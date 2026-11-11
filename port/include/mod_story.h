#ifndef MOD_STORY_H
#define MOD_STORY_H
#include "mod_select.h"
/* Dragon History revamp: port-side story chapters. Story progress is separate
   from Scenario trophies. Text of record: SAIYAN-SAGA-SCRIPT-ES-EN.md.
   Chapter ids and progress bits are stable: never renumber or reuse them. */
#define ST_COUNT 22 /* (+2 scene-only interludes after chapter 5) */ /* chapters 4-17: Saiyan saga script nodes 05-17 (Claude, 2026-11-02, proposals for Juan) */
/* Flip only once the complete authored campaign is present. Future chapters
   must be appended, never renumbered; a demo chapter is not a campaign clear. */
#define ST_CAMPAIGN_FINALIZED 0
/* A retired chapter (its ID and save bits kept, out of the menu): old chapters 8-11 live on inside chapter 7. */
#define ST_SAGA_RETIRED 99
/* A scene-only interlude (no fight, no rewards, out of the menu): played on its own stage after the chapter that
   names it in stInterludeNext. */
#define ST_SAGA_INTERLUDE 98
#define ST_RANK_CAPACITY 72
#if ST_COUNT > ST_RANK_CAPACITY
#error Dragon_History_rank_save_capacity_exceeded
#endif
#define ST_LINES 8
enum { ST_NONE, ST_CANON, ST_ALT, ST_FAIL };
/* progress bits per chapter (saved): */
#define ST_DONE_CANON 1u
#define ST_DONE_ALT 2u
typedef struct StLine {const char *speaker,*text[2];} StLine;
typedef struct StChapter {
 const char *id;int saga,sequence;
 const char *title[2],*recap[2],*objective[2];
 int player,playerCostume,rival,rivalCostume,stage;const char *music;
 int introCount,canonCount,altCount;
 StLine intro[ST_LINES],canon[ST_LINES],alt[ST_LINES];
 int zeniCanonFirst,zeniCanonRepeat,zeniAltFirst,zeniAltRepeat;
} StChapter;
/* Zeni amounts are placeholders until Juan sets them. */
static const StChapter stChapters[ST_COUNT]={
 {"saiyan_01_raditz_wasteland",0,1,
  {"An unfamiliar presence","Una presencia desconocida"},
  {"An unfamiliar power has reached Earth. Piccolo confronts its source.","Una energ\303\255a desconocida acaba de llegar a la Tierra. Piccoro se interpone en su camino."},
  {"Test his strength.","Mide su fuerza."},
  22,1,77,0,0,"Saga de los Saiyajin",
  3,3,2,
  {{"Piccolo",{"I haven't seen you before. Explain what you're looking for.","No te he visto antes. Ser\303\241 mejor que expliques qu\303\251 buscas."}}, /* SS01_001 */
   {"Raditz",{"I'm looking for someone. You're barely worth stopping for.","Busco a alguien. T\303\272 apenas mereces que me detenga."}}, /* SS01_002 */
   {"Piccolo",{"Then you'll have to get past me.","Entonces tendr\303\241s que pasar por encima de m\303\255."}}}, /* SS01_003 */
  {{"Piccolo",{"I hit him clean... and he didn't even move!","\302\241Le di de lleno... y ni siquiera retrocedi\303\263!"}}, /* SS01_004 */
   {"Raditz",{"That power... At last, I found you, Kakarot.","Esa energ\303\255a... Por fin te encontr\303\251, Kakarotto."}}, /* SS01_005 */
   {"Piccolo",{"So he wasn't here for me. He's after Goku.","As\303\255 que no vino por m\303\255. Busca a Goku."}}}, /* SS01_006 */
  {{"Piccolo",{"Was that your idea of looking down on Earthlings? Judge your opponent better next time.","\302\277Eso era despreciar a los terr\303\255colas? La pr\303\263xima vez, mide mejor a tu rival."}}, /* SSA_001 */
   {"Raditz",{"This isn't over. I still have someone to find.","Esto no ha terminado. Todav\303\255a tengo a qui\303\251n buscar."}}}, /* SSA_002 */
  1000,200,3000,500},
 {"saiyan_02_kame_house",0,2,
  {"The visitor at Kame House","El visitante de Kame House"},
  /* DRAFT recap/objective: the script has none for node 02 */
  {"A stranger lands at Kame House and calls Goku by another name.","Un desconocido llega a Kame House y llama a Goku por otro nombre."},
  {"Protect Gohan.","Protege a Gohan."},
  0,2,77,0,12,"Saga de los Saiyajin",
  8,4,2,
  {{"Goku",{"Say hello, Gohan. You don't have to hide behind me.","Gohan, saluda. No tienes que esconderte detr\303\241s de m\303\255."}}, /* SS02_001 */
   {"Gohan",{"Hello...","Hola..."}}, /* SS02_002 */
   {"Raditz",{"Kakarot. You've grown far too comfortable on this planet.","Kakarotto. Te has acostumbrado demasiado a este planeta."}}, /* SS02_003 */
   {"Goku",{"My name is Goku. Where did you hear that name?","Me llamo Goku. \302\277De d\303\263nde has sacado ese nombre?"}}, /* SS02_004 */
   {"Raditz",{"I'm Raditz, your brother. We're both Saiyans.","Soy Raditz, tu hermano. Los dos somos Saiyajin."}}, /* SS02_005 */
   {"Goku",{"My brother...? I don't know anything about that.","\302\277Mi hermano...? No s\303\251 nada de eso."}}, /* SS02_006 */
   {"Raditz",{"You were sent to conquer this world, not start a family.","Te enviaron a conquistar este mundo, no a formar una familia."}}, /* SS02_007 */
   {"Goku",{"Then they chose the wrong person. I'm not hurting anyone.","Pues se equivocaron conmigo. No voy a hacerle da\303\261o a nadie."}}}, /* SS02_008 */
  {{"Gohan",{"Dad!","\302\241Pap\303\241!"}}, /* SS02_009 */
   {"Raditz",{"Kill a hundred Earthlings and join us. Refuse, and you lose your son.","Mata a cien terr\303\255colas y \303\272nete a nosotros. Si te niegas, perder\303\241s a tu hijo."}}, /* SS02_010 */
   {"Goku",{"Gohan! Wait!","\302\241Gohan! \302\241Espera!"}}, /* SS02_011 */
   {"Krillin",{"You can't go after him alone.","No puedes ir detr\303\241s de \303\251l t\303\272 solo."}}}, /* SS02_012 */
  {{"Krillin",{"Goku, hold on! He can't keep fighting!","\302\241Goku, aguanta! \302\241Ya no puede seguir!"}}, /* SSA_014 */
   /* DRAFT */
   {"Raditz",{"This isn't over, Kakarot. I'll be back for you.","Esto no ha terminado, Kakarotto. Volver\303\251 por ti."}}},
  1000,200,3000,500},
 {"saiyan_03_unlikely_alliance",0,3,
  {"An unlikely alliance","Una alianza imposible"},
  {"Goku and Piccolo put their rivalry aside. Gohan is still a prisoner, and Raditz is waiting.","Goku y Piccoro dejan su rivalidad a un lado. Gohan sigue prisionero y Raditz los espera."},
  {"Rescue Gohan.","Rescata a Gohan."},
  0,3,77,0,0,"Saga de los Saiyajin",
  3,5,2,
  {{"Raditz",{"So you brought Piccolo. It won't save you.","As\303\255 que trajiste a Piccoro. No te servir\303\241."}}, /* SS03_001 */
   {"Goku",{"Let my son go. Then you can fight me.","Suelta a mi hijo. Despu\303\251s podr\303\241s pelear conmigo."}}, /* SS03_002 */
   {"Piccolo",{"He won't listen. Keep him busy.","No va a escuchar. Mantenlo ocupado."}}, /* SS03_003 */},
  {{"Piccolo",{"Goku can come back. The Dragon Balls can bring him back.","Goku puede volver. Las Esferas del Drag\303\263n pueden revivirlo."}}, /* SS04_005 */
   {"Raditz",{"My scouter... they heard everything.","Mi rastreador... lo han o\303\255do todo."}}, /* SS04_006 */
   {"Piccolo",{"Who did?","\302\277Qui\303\251nes?"}}, /* SS04_007 */
   {"Raditz",{"Two Saiyans will arrive in a year. I'm nothing compared to them.","Dos Saiyajin llegar\303\241n dentro de un a\303\261o. Yo no soy nada a su lado."}}, /* SS04_008 */
   {"Goku",{"Krillin... look after Gohan until I return.","Krilin... cuida de Gohan hasta que vuelva."}}, /* SS04_009 */},
  {{"Goku",{"Gohan's safe... and this time we're going home together.","Gohan est\303\241 a salvo... y esta vez volvemos juntos."}}, /* SSA_003 */
   {"Piccolo",{"Don't get used to everything going well. What he said about the other Saiyans worries me.","No te acostumbres a que todo salga bien. Lo que dijo de los otros Saiyajin me preocupa."}}, /* SSA_004 */},
  1000,200,3000,500},
 {"saiyan_04_gohans_power",0,4,
  {"Gohan's hidden power","El poder de Gohan"},
  {"Goku is dead. Piccolo has seen Gohan's hidden power and decides to prepare him for what is coming.","Goku ha muerto. Piccoro ha visto el poder que esconde Gohan y decide prepararlo para lo que viene."},
  {"Stop his rampage.","Det\303\251n su furia."},
  22,1,13,0,24,"Saga de los Saiyajin",
  4,1,1,
  {{"Gohan",{"I want my mom. I don't know how to fight.","Quiero ir con mi mam\303\241. No s\303\251 pelear."}} /* SS05_001 */,
   {"Piccolo",{"That's why you're here. What's coming won't wait until you're ready.","Por eso est\303\241s aqu\303\255. Lo que viene no va a esperar a que est\303\251s preparado."}} /* SS05_002 */,
   {"Gohan",{"You're leaving me alone?","\302\277Me vas a dejar solo?"}} /* SS05_003 */,
   {"Piccolo",{"Learn to stand on your own. I'll be closer than you think.","Aprende a mantenerte en pie. Yo estar\303\251 m\303\241s cerca de lo que crees."}} /* SS05_004 */},
  {{"Piccolo",{"Sleep, kid. We continue tomorrow.","Duerme, chico. Ma\303\261ana seguimos."}} /* SS05_009 */},
  {{"Piccolo",{"It's over, Gohan. I won't let this happen to you again.","Ya est\303\241, Gohan. No voy a dejar que esto vuelva a pasarte."}} /* SSA_005 */},
  1000,200,3000,500},
 {"saiyan_05_one_year",0,5,
  {"One year to prepare","Un a\303\261o para cambiarlo todo"},
  {"Piccolo trains Gohan in the wilderness while Goku races toward King Kai.","Piccoro entrena a Gohan en las tierras salvajes mientras Goku corre hacia Kaio-sama."},
  {"Keep training.","Sigue entrenando."},
  13,0,22,1,1,"Saga de los Saiyajin",
  2,2,0,
  {{"Piccolo",{"Eyes forward. Don't close them when I strike.","Mira al frente. No cierres los ojos cuando ataque."}} /* SS06_001 */,
   {"Gohan",{"I saw it that time... but I was still too slow.","Esta vez lo vi... pero todav\303\255a fui muy lento."}} /* SS06_002 */},
  {{"Piccolo",{"You've stopped running away. Tomorrow, we push further.","Ya no huyes de cada golpe. Ma\303\261ana iremos m\303\241s lejos."}},
   {"Gohan",{"I'll get stronger. I want to protect everyone too.","Me har\303\251 m\303\241s fuerte. Yo tambi\303\251n quiero proteger a los dem\303\241s."}}},
  {{0,{0,0}}},
  1000,200,3000,500},
 {"saiyan_06_lookout",0,6,
  {"Kami's Lookout","El Templo de Kami"},
  {"Krillin, Yamcha, Tien, Chiaotzu and Yajirobe train with Mr. Popo. Gameplay adaptation: they face their own doubles.","Krilin, Yamcha, Tenshinhan, Chaozu y Yajirobe entrenan con Mr. Popo. Adaptaci\303\263n jugable: se enfrentan a sus dobles."},
  {"Face your doubts.","Enfrenta tus dudas."},
  25,0,25,0,5,"Saga de los Saiyajin",
  5,1,0,
  {{"Krillin",{"If we've all come this far, we'd better make it count.","Si todos hemos venido hasta aqu\303\255, ser\303\241 mejor que lo aprovechemos."}} /* SS06_010 */,
   {"Yamcha",{"For once, I don't mind ending the day completely exhausted.","Por una vez, no me importa terminar el d\303\255a hecho polvo."}} /* SS06_011 */,
   {"Tenshinhan",{"We're not training to win a tournament. This time, Earth is at stake.","No entrenamos para ganar un torneo. Esta vez nos jugamos la Tierra."}} /* SS06_012 */,
   {"Chaozu",{"Then we'll train together.","Entonces entrenaremos juntos."}} /* SS06_013 */,
   {"Mr. Popo",{"Strength is useless if fear makes your decisions.","La fuerza no sirve si el miedo decide por ustedes."}} /* SS06_014 */},
  {{"Mr. Popo",{"That's it. Fight as a team.","Eso es. Peleen como un equipo."}} /* SS06_019 */,
   {"Roshi",{"Shenron, restore Goku to life. Earth needs him.","Shenlong, devuelve la vida a Goku. La Tierra lo necesita."}} /* SS06_020 */,
   {"King Kai",{"They've arrived sooner than expected. Hurry!","Han llegado antes de lo previsto. \302\241Apres\303\272rate!"}} /* SS06_021 */,
   {"Goku",{"Hold on a little longer! I'm coming!","\302\241Resistan un poco m\303\241s! \302\241Ya voy!"}} /* SS06_022 */},
  {{0,{0,0}}},
  1000,200,3000,500},
 {"saiyan_07_saiyans_arrive",0,8,
  {"Earth's last defense","La \303\272ltima defensa de la Tierra"},
  {"The year of preparation is over. Two pods cross the sky, and Goku is still far away.","El a\303\261o de preparaci\303\263n ha terminado. Dos c\303\241psulas atraviesan el cielo y Goku a\303\272n est\303\241 lejos."},
  {"Hold the line until Goku arrives.","Resiste hasta que llegue Goku."},
  27,0,79,0,0,"Saga de los Saiyajin",
  8,4,1,
  {{"Nappa",{"So this is the planet Raditz died for.","As\303\255 que este es el planeta por el que muri\303\263 Raditz."}} /* SS07_001 */,
   {"Vegeta",{"Find the ones who beat him. The balls can wait.","Encuentra a los que lo vencieron. Las esferas vendr\303\241n despu\303\251s."}} /* SS07_002 */,
   {"Krillin",{"It's them. There's no mistaking that energy.","Son ellos. No hay forma de confundir esa energ\303\255a."}} /* SS07_003 */,
   {"Gohan",{"Mr. Piccolo...","Se\303\261or Piccoro..."}} /* SS07_004 */,
   {"Piccolo",{"Breathe. Remember what you learned.","Respira. Recuerda lo que aprendiste."}} /* SS07_005 */,
   {"Vegeta",{"A Namekian. That explains a few things.","Un namekiano. Eso explica algunas cosas."}} /* SS07_006 */,
   {"Piccolo",{"I'm here to stop you, not answer questions.","He venido a detenerte, no a responder preguntas."}} /* SS07_007 */,
   {"Nappa",{"First, let's see how long they last against something smaller.","Primero veremos cu\303\241nto duran con algo m\303\241s peque\303\261o."}} /* SS07_008 */},
  {{"Nappa",{"Vegeta... help me.","Vegeta... ay\303\272dame."}} /* SS11_008 */,
   {"Vegeta",{"I have no use for someone who can no longer fight.","No necesito a alguien que ya no puede luchar."}} /* SS11_009 */,
   {"Goku",{"He was your partner.","Era tu compa\303\261ero."}} /* SS11_010 */,
   {"Vegeta",{"And now I'm your problem.","Y ahora tu problema soy yo."}} /* SS11_011 */},
  {{"Narrator",{"Alternate victory. This outcome changes what happened in this encounter.","Victoria alternativa. Este desenlace cambia lo ocurrido en este encuentro."}} /* SSA_016 */},
  1000,200,3000,500},
 {"saiyan_08_six_seeds",ST_SAGA_RETIRED,8,
  {"Six seeds","Seis semillas"},
  {"Tien has won the first round. Yamcha steps forward for the next Saibaman.","Tenshinhan gan\303\263 el primer combate. Yamcha da un paso al frente para el siguiente Saibaiman."},
  {"Defeat the Saibaman.","Derrota al Saibaiman."},
  26,0,79,0,0,"Saga de los Saiyajin",
  3,2,2,
  {{"Yamcha",{"Stay back, Krillin. I've got this one.","Krilin, qu\303\251date atr\303\241s. Yo me encargo."}} /* SS08_004 */,
   {"Krillin",{"Don't let your guard down!","\302\241No te conf\303\255es!"}} /* SS08_005 */,
   {"Yamcha",{"I can handle one Saibaiman.","Puedo con un Saibaiman."}} /* SS08_006 */},
  {{"Gohan",{"I didn't see it coming...","No lo vi venir..."}} /* SS08_011 */,
   {"Piccolo",{"That's why you keep your eyes on it.","Por eso no debes apartar la mirada."}} /* SS08_012 */},
  {{"Yamcha",{"I almost let my guard down. Thanks for the warning.","Casi me conf\303\255o. Gracias por avisarme."}} /* SSA_006 */,
   {"Krillin",{"Next time, we celebrate after it stops moving.","La pr\303\263xima vez, lo celebramos cuando deje de moverse."}} /* SSA_007 */},
  1000,200,3000,500},
 {"saiyan_09_nappa",ST_SAGA_RETIRED,9,
  {"The wall called Nappa","El muro llamado Nappa"},
  {"The Saibamen are gone, but so is Yamcha. Nappa is done watching.","Los Saibaiman han ca\303\255do, pero Yamcha tambi\303\251n. Nappa ya no tiene intenci\303\263n de seguir mirando."},
  {"Hold the line.","Resiste."},
  27,0,80,0,0,"Saga de los Saiyajin",
  3,1,2,
  {{"Nappa",{"Is that all you can do?","\302\277Eso es todo lo que pueden hacer?"}} /* SS09_001 */,
   {"Tenshinhan",{"Stay together. Don't let him split us up.","Mant\303\251nganse juntos. No dejen que nos separe."}} /* SS09_002 */,
   {"Chaozu",{"Tenshinhan!","\302\241Tenshinhan!"}} /* SS09_003 */},
  {{"Nappa",{"Even that couldn't bring me down.","Ni siquiera eso pudo derribarme."}} /* SS09_013 */},
  {{"Tenshinhan",{"We did it! Chiaotzu, stay with me.","\302\241Lo conseguimos! Chaozu, qu\303\251date conmigo."}} /* SSA_008 */,
   {"Nappa",{"No... I couldn't have lost to them.","No... No puedo haber perdido contra ellos."}} /* SSA_009 */},
  1000,200,3000,500},
 {"saiyan_10_three_hours",ST_SAGA_RETIRED,10,
  {"Three hours of hope","Tres horas de esperanza"},
  {"Vegeta gives Kakarot three hours. Piccolo, Krillin and Gohan face Nappa again.","Vegeta le da tres horas a Kakarotto. Piccoro, Krilin y Gohan vuelven a enfrentarse a Nappa."},
  {"Protect Gohan.","Protege a Gohan."},
  22,1,80,0,0,"Saga de los Saiyajin",
  5,2,2,
  {{"Krillin",{"Goku will come. We just have to hold on a little longer.","Goku vendr\303\241. Solo tenemos que resistir un poco m\303\241s."}} /* SS10_001 */,
   {"Vegeta",{"Kakarot... Very well. I'll give him three hours.","Kakarotto... Muy bien. Le dar\303\251 tres horas."}} /* SS10_002 */,
   {"Nappa",{"And if he doesn't come?","\302\277Y si no aparece?"}} /* SS10_003 */,
   {"Vegeta",{"Then finish them. I won't wait any longer.","Entonces acaba con ellos. No esperar\303\251 m\303\241s."}} /* SS10_004 */,
   {"Narrator",{"Three hours later...","Tres horas despu\303\251s..."}} /* SS10_005 */},
  {{"Krillin",{"Without Piccolo, the Dragon Balls are gone too.","Sin Piccoro, las Esferas del Drag\303\263n tambi\303\251n desaparecen."}} /* SS10_017 */,
   {"Gohan",{"I'll never forgive you!","\302\241No te lo perdonar\303\251!"}} /* SS10_018 */},
  {{"Gohan",{"Mr. Piccolo! We're still here!","\302\241Se\303\261or Piccoro! \302\241Seguimos aqu\303\255!"}} /* SSA_010 */,
   {"Piccolo",{"And we will be if we keep our guard up.","Y seguiremos si no bajamos la guardia."}} /* SSA_011 */},
  1000,200,3000,500},
 {"saiyan_11_goku_returns",ST_SAGA_RETIRED,11,
  {"Goku returns","Goku regresa"},
  {"Goku arrives too late for several of his friends. Gohan and Krillin still need him.","Goku llega demasiado tarde para varios de sus amigos. Gohan y Krilin todav\303\255a lo necesitan."},
  {"Defeat Nappa.","Derrota a Nappa."},
  0,3,80,0,0,"Saga de los Saiyajin",
  4,4,0,
  {{"Goku",{"Gohan... Krillin... tell me who else fell.","Gohan... Krilin... d\303\255ganme qui\303\251nes m\303\241s cayeron."}} /* SS11_001 */,
   {"Krillin",{"Yamcha, Tien, Chiaotzu... Piccolo saved Gohan.","Yamcha, Tenshinhan, Chaozu... Piccoro salv\303\263 a Gohan."}} /* SS11_002 */,
   {"Goku",{"I'm sorry. I'm here now.","Perd\303\263nenme. Ya estoy aqu\303\255."}} /* SS11_003 */,
   {"Nappa",{"So you're Kakarot. Let's see what you can do.","As\303\255 que t\303\272 eres Kakarotto. Veamos qu\303\251 sabes hacer."}} /* SS11_004 */},
  {{"Nappa",{"Vegeta... help me.","Vegeta... ay\303\272dame."}} /* SS11_008 */,
   {"Vegeta",{"I have no use for someone who can no longer fight.","No necesito a alguien que ya no puede luchar."}} /* SS11_009 */,
   {"Goku",{"He was your partner.","Era tu compa\303\261ero."}} /* SS11_010 */,
   {"Vegeta",{"And now I'm your problem.","Y ahora tu problema soy yo."}} /* SS11_011 */},
  {{0,{0,0}}},
  1000,200,3000,500},
 {"saiyan_12_kaioken",0,9,
  {"The battle for Earth","La batalla por la Tierra"},
  {"With Nappa gone, Vegeta stops watching. Goku must push his new power beyond its limits.","Sin Nappa, Vegeta deja de observar. Goku tendr\303\241 que superar los l\303\255mites de su nuevo poder."},
  {"Stop Vegeta.","Det\303\251n a Vegeta."},
  0,3,29,0,1,"Saga de los Saiyajin",
  3,5,1,
  {{"Vegeta",{"A low-class warrior can't match a Saiyan prince.","Un guerrero de clase baja no puede igualar a un pr\303\255ncipe Saiyajin."}} /* SS12_001 */,
   {"Goku",{"I don't mind being born low-class. Earth taught me your birth doesn't decide how far you can go.","No me importa haber nacido de clase baja. En la Tierra aprend\303\255 que tu origen no decide hasta d\303\263nde puedes llegar."}} /* SS12_002 */,
   {"Goku",{"I can still surpass you!","\302\241Todav\303\255a puedo superarte!"}} /* SS12_003 */},
  {{"Gohan",{"Mr. Piccolo came from another planet. Vegeta called him a Namekian.","El se\303\261or Piccoro era de otro planeta. Vegeta lo llam\303\263 namekiano."}} /* SS17_010 */,
   {"Krillin",{"If they have Dragon Balls there too... maybe we can bring everyone back.","Si all\303\255 tambi\303\251n hay Esferas del Drag\303\263n... quiz\303\241 podamos traerlos de vuelta."}} /* SS17_011 */,
   {"Bulma",{"First we'll find that planet. Then we'll work out how to get there.","Primero encontraremos ese planeta. Despu\303\251s veremos c\303\263mo llegar."}} /* SS17_012 */,
   {"Gohan",{"Then I'm coming with you. I'm not giving up now.","Entonces voy con ustedes. No pienso rendirme ahora."}} /* SS17_013 */,
   {"Narrator",{"Earth still stands. The hope of bringing their friends back now lies on Namek.","La Tierra sigue en pie. La esperanza de recuperar a sus amigos apunta ahora hacia Namek."}} /* SS17_014 */},
  {{"Narrator",{"Alternate victory. This outcome changes what happened in this encounter.","Victoria alternativa. Este desenlace cambia lo ocurrido en este encuentro."}} /* SSA_016 */},
  1000,200,3000,500},
 {"saiyan_13_artificial_moon",ST_SAGA_RETIRED,9,
  {"An artificial moon","Una luna artificial"},
  {"Vegeta has found another way to fight: the power of a Saiyan under the moon.","Vegeta ha encontrado otra forma de pelear: el poder de un Saiyajin bajo la luna."},
  {"Find an opening.","Busca una oportunidad."},
  0,3,30,0,26,"Saga de los Saiyajin",
  3,1,2,
  {{"Vegeta",{"The moon is gone. I'll make another.","La luna ya no est\303\241. Har\303\251 otra."}} /* SS13_001 */,
   {"Goku",{"What are you doing?","\302\277Qu\303\251 est\303\241s haciendo?"}} /* SS13_002 */,
   {"Vegeta",{"Now you'll see a Saiyan's true power.","Ahora conocer\303\241s el verdadero poder Saiyajin."}} /* SS13_003 */},
  {{"Vegeta",{"I won't give you time!","\302\241No te dar\303\251 tiempo!"}} /* SS13_008 */},
  {{"Goku",{"Enough, Vegeta. You can still leave.","Ya basta, Vegeta. Todav\303\255a puedes irte."}} /* SSA_012 */,
   {"Vegeta",{"Don't think I'll forget this humiliation.","No creas que voy a olvidar esta humillaci\303\263n."}} /* SSA_013 */},
  1000,200,3000,500},
 {"saiyan_14_yajirobe",ST_SAGA_RETIRED,10,
  {"Yajirobe's courage","El valor de Yajirobe"},
  {"Goku can fight no longer. His friends return, though none can overpower the giant.","Goku ya no puede continuar. Sus amigos regresan, aunque ninguno puede vencer al gigante por la fuerza."},
  {"Distract Vegeta.","Distrae a Vegeta."},
  13,0,30,0,26,"Saga de los Saiyajin",
  3,1,1,
  {{"Gohan",{"Let go of my dad!","\302\241Suelta a mi pap\303\241!"}} /* SS14_001 */,
   {"Krillin",{"Gohan, stay clear of his tail!","Gohan, \302\241al\303\251jate de su cola!"}} /* SS14_002 */,
   {"Yajirobe",{"I came to watch... not fight a giant ape!","Yo ven\303\255a a mirar... \302\241no a pelear con un mono gigante!"}} /* SS14_003 */},
  {{"Yajirobe",{"Don't shout! Now he's after me!","\302\241No grites! \302\241Ahora viene por m\303\255!"}} /* SS14_009 */},
  {{"Krillin",{"Goku, hold on! He can't keep fighting!","\302\241Goku, aguanta! \302\241Ya no puede seguir!"}} /* SSA_014 */},
  1000,200,3000,500},
 {"saiyan_15_last_energy",ST_SAGA_RETIRED,11,
  {"The last of our energy","La \303\272ltima energ\303\255a"},
  {"Vegeta has his tail no more, but he is still far stronger. Goku has one gift left to give.","Vegeta ya no tiene cola, pero sigue siendo mucho m\303\241s fuerte. A Goku le queda algo que entregar."},
  {"Buy time.","Gana tiempo."},
  13,0,29,0,26,"Saga de los Saiyajin",
  3,3,1,
  {{"Gohan",{"Get to Dad! I'll keep Vegeta busy.","\302\241Ve por pap\303\241! Yo distraer\303\251 a Vegeta."}} /* SS15_001 */,
   {"Krillin",{"Goku, save your strength. I'll take the Spirit Bomb.","Goku, guarda tus fuerzas. Yo me encargo de la Genkidama."}} /* SS15_002 */,
   {"Goku",{"There's still energy in it. Krillin, throw it when you see an opening!","A\303\272n queda energ\303\255a. Krilin, \302\241l\303\241nzala cuando veas una abertura!"}} /* SS15_003 */},
  {{"Krillin",{"It hit him!","\302\241Le dio!"}} /* SS15_011 */,
   {"Gohan",{"Did we get him...?","\302\277Lo derrotamos...?"}} /* SS15_012 */,
   {"Vegeta",{"Not... yet.","Todav\303\255a... no."}} /* SS15_013 */},
  {{"Gohan",{"This time, we made it in time.","Esta vez llegamos a tiempo."}} /* SSA_015 */},
  1000,200,3000,500},
 {"saiyan_16_last_giant",ST_SAGA_RETIRED,12,
  {"The last giant","El \303\272ltimo gigante"},
  {"Vegeta survives the Spirit Bomb. But the artificial moon still shines, and Gohan's tail has grown back.","Vegeta sobrevive a la Genkidama. Pero la luna artificial a\303\272n brilla, y la cola de Gohan ha vuelto a crecer."},
  {"Survive both giants' fury.","Sobrevive."},
  25,0,29,0,26,"Saga de los Saiyajin",
  3,2,1,
  {{"Vegeta",{"His tail... Gohan, look out!","Su cola... \302\241Gohan, cuidado!"}} /* SS16_001 */,
   {"Goku",{"Gohan... look up.","Gohan... mira hacia arriba."}} /* SS16_002 */,
   {"Krillin",{"The artificial moon is still there!","\302\241La luna artificial sigue ah\303\255!"}} /* SS16_003 */},
  {{"Krillin",{"Gohan... he's breathing. He's alive.","Gohan... respira. Sigue vivo."}} /* SS16_007 */,
   {"Vegeta",{"I... can't get up.","No... puedo levantarme."}} /* SS16_008 */},
  {{"Gohan",{"This time, we made it in time.","Esta vez llegamos a tiempo."}} /* SSA_015 */},
  1000,200,3000,500},
 {"saiyan_17_let_him_live",ST_SAGA_RETIRED,13,
  {"Let him live","Dejarlo vivir"},
  {"Vegeta can barely move. Krillin holds Yajirobe's sword.","Vegeta apenas puede moverse. Krilin sostiene la espada de Yajirobe."},
  {"Let him go.","D\303\251jalo ir."},
  25,0,29,0,26,"Saga de los Saiyajin",
  7,5,0,
  {{"Krillin",{"After everything he did... we should finish this!","Despu\303\251s de todo lo que hizo... \302\241debemos acabar con esto!"}} /* SS17_001 */,
   {"Goku",{"Krillin... wait. Let him go.","Krilin... espera. D\303\251jalo ir."}} /* SS17_002 */,
   {"Krillin",{"Goku, think of Yamcha. Think of the others.","Goku, piensa en Yamcha. Piensa en los dem\303\241s."}} /* SS17_003 */,
   {"Goku",{"I haven't forgotten them. I'll face Vegeta again when we're stronger.","No los he olvidado. Volver\303\251 a enfrentarme a Vegeta cuando seamos m\303\241s fuertes."}} /* SS17_004 */,
   {"Krillin",{"You're asking a lot of us.","Nos est\303\241s pidiendo mucho."}} /* SS17_005 */,
   {"Goku",{"I know. This time, I'm asking for myself.","Lo s\303\251. Esta vez te lo pido por m\303\255."}} /* SS17_006 */,
   {"Krillin",{"All right. But be ready when he comes back.","Est\303\241 bien. Pero prep\303\241rate cuando vuelva."}} /* SS17_007 */},
  {{"Gohan",{"Mr. Piccolo came from another planet. Vegeta called him a Namekian.","El se\303\261or Piccoro era de otro planeta. Vegeta lo llam\303\263 namekiano."}} /* SS17_010 */,
   {"Krillin",{"If they have Dragon Balls there too... maybe we can bring everyone back.","Si all\303\255 tambi\303\251n hay Esferas del Drag\303\263n... quiz\303\241 podamos traerlos de vuelta."}} /* SS17_011 */,
   {"Bulma",{"First we'll find that planet. Then we'll work out how to get there.","Primero encontraremos ese planeta. Despu\303\251s veremos c\303\263mo llegar."}} /* SS17_012 */,
   {"Gohan",{"Then I'm coming with you. I'm not giving up now.","Entonces voy con ustedes. No pienso rendirme ahora."}} /* SS17_013 */,
   {"Narrator",{"Earth still stands. The hope of bringing their friends back now lies on Namek.","La Tierra sigue en pie. La esperanza de recuperar a sus amigos apunta ahora hacia Namek."}} /* SS17_014 */},
  {{0,{0,0}}},
  1000,200,3000,500},
 {"saiyan_05b_lookout_training",ST_SAGA_INTERLUDE,5,
  {"Meanwhile, at Kami's Lookout","Mientras tanto, en el Templo de Kami"},
  {"Earth's warriors gather at Kami's Lookout to prepare for the Saiyans.","Los guerreros de la Tierra se re\303\272nen en el Templo de Kami para prepararse contra los Saiyajin."},
  {"",""},
  25,0,27,0,5,"Saga de los Saiyajin",
  4,0,0,
  {{"Narrator",{"One year. That is all the time Earth has left before the Saiyans arrive.","Un a\303\261o. Es todo el tiempo que le queda a la Tierra antes de que lleguen los Saiyajin."}} /* DRAFT narrator */,
   {"Narrator",{"High above the world, at Kami's Lookout, Earth's warriors have gathered to train.","En lo alto del mundo, en el Templo de Kami, los guerreros de la Tierra se han reunido para entrenar."}} /* DRAFT narrator */,
   {"Narrator",{"Krillin, Yamcha, Tien, Chiaotzu and Yajirobe push past their limits under Kami and Mr. Popo.","Krilin, Yamcha, Tenshinhan, Chaozu y Yajirobe superan sus l\303\255mites bajo la gu\303\255a de Kami y Mr. Popo."}} /* DRAFT narrator */,
   {"Narrator",{"Meanwhile, at the end of Snake Way, Goku's own training has only just begun...","Mientras tanto, al final del Camino de la Serpiente, el entrenamiento de Goku apenas ha comenzado..."}} /* DRAFT narrator */},
  {{0,{0,0}}},
  {{0,{0,0}}},
  0,0,0,0},
 {"saiyan_05c_king_kai",ST_SAGA_INTERLUDE,5,
  {"King Kai's planet","El planeta de Kaio-sama"},
  {"Goku trains with King Kai at the end of Snake Way.","Goku entrena con Kaio-sama al final del Camino de la Serpiente."},
  {"",""},
  0,3,208,0,7,"Saga de los Saiyajin",
  6,0,0,
  {{"Narrator",{"On a tiny planet in the Other World, where gravity is ten times Earth's, Goku trains with King Kai.","En un peque\303\261o planeta del Otro Mundo, con diez veces la gravedad de la Tierra, Goku entrena con Kaio-sama."}} /* DRAFT narrator */,
   {"Goku",{"Every step feels so heavy... Good. I can get stronger here.","Cada paso pesa much\303\255simo... Perfecto. Aqu\303\255 puedo hacerme m\303\241s fuerte."}} /* SS06_005 */,
   {"King Kai",{"Kaioken demands more from your body than you think. Don't push it recklessly.","El Kaioken exige m\303\241s de tu cuerpo de lo que crees. No lo fuerces sin pensar."}} /* SS06_006 */,
   {"Goku",{"I'll use it when I need it. What about the other technique?","Lo usar\303\251 cuando haga falta. \302\277Y esa otra t\303\251cnica?"}} /* SS06_007 */,
   {"King Kai",{"The Spirit Bomb gathers energy from the life around you. You need time and calm.","La Genkidama re\303\272ne energ\303\255a de la vida que te rodea. Necesitas tiempo y calma."}} /* SS06_008 */,
   {"Goku",{"Then I'll learn to find both in the middle of a fight.","Entonces aprender\303\251 a conseguir ambas cosas en plena pelea."}} /* SS06_009 */},
  {{0,{0,0}}},
  {{0,{0,0}}},
  0,0,0,0},
 {"saiyan_06b_wish",ST_SAGA_INTERLUDE,6,
  {"A wish for Earth","Un deseo por la Tierra"},{"",""},{"",""},66,0,0,0,12,"Saga de los Saiyajin",1,0,0,
  {{"Roshi",{"Shenron, restore Goku to life. Earth needs him.","Shenlong, devuelve la vida a Goku. La Tierra lo necesita."}}},{{0}},{{0}},0,0,0,0},
 {"saiyan_06c_hurry",ST_SAGA_INTERLUDE,6,
  {"Hurry, Goku!","\302\241Apres\303\272rate, Goku!"},{"",""},{"",""},0,0,208,0,7,"Saga de los Saiyajin",2,0,0,
  {{"King Kai",{"They've arrived sooner than expected. Hurry!","Han llegado antes de lo previsto. \302\241Apres\303\272rate!"}},
   {"Goku",{"Hold on a little longer! I'm coming!","\302\241Resistan un poco m\303\241s! \302\241Ya voy!"}}},{{0}},{{0}},0,0,0,0}

,
 {"saiyan_06d_pendulum_saiyans",0,7,
 {"Saiyans from the past","Los Saiyajin del pasado"},
 {"Kami's next trial takes the warriors to the ruins of a distant world. Two Saiyans are waiting, and this time teamwork is their only way home.","La siguiente prueba de Kami lleva a los guerreros a las ruinas de un mundo lejano. Dos Saiyajin los esperan; solo luchando juntos podr\303\241n regresar."},
 {"Defeat Shorty and Scarface. Protect your teammates.","Derrota a Shorty y Scarface. Protege a tus compa\303\261eros."},
 25,0,323,0,15,"Saga de los Saiyajin",5,3,0,
 {{"Krillin",{"Where did the Lookout go? Is this really another planet?","\302\277D\303\263nde est\303\241 el Templo? \302\277De verdad estamos en otro planeta?"}},
  {"Yamcha",{"Stay together. I don't like those two watching us.","Sigamos juntos. No me gusta c\303\263mo nos miran esos dos."}},
  {"Shorty",{"Look at this! Weaklings came all this way to die.","\302\241Mira esto! Unos debiluchos vinieron hasta aqu\303\255 para morir."}},
  {"Scarface",{"Don't finish them too quickly. Let them fight back.","No acabes con ellos tan deprisa. Deja que se defiendan."}},
  {"Tenshinhan",{"Cover each other. We didn't come here to run away!","Cubramos nuestras espaldas. \302\241No vinimos a huir!"}}},
 {{"Krillin",{"We did it... but I could barely keep up with them!","Lo logramos... \302\241pero apenas pod\303\255a seguirles el ritmo!"}},
  {"Tenshinhan",{"Remember that feeling. The Saiyans coming to Earth will be stronger.","Recuerden esta sensaci\303\263n. Los Saiyajin que vienen a la Tierra ser\303\241n m\303\241s fuertes."}},
  {"Yamcha",{"Then we keep training. Next time, none of us fights alone.","Entonces seguiremos entrenando. La pr\303\263xima vez, nadie luchar\303\241 solo."}}},
 {{0}},1500,300,3500,600}
};
/* A saga is complete only once its full authored arc is finalized, not when
   the last currently available demo chapter has been cleared. */
#define ST_SAGA_COUNT 8
static const unsigned char stSagaFinalized[ST_SAGA_COUNT]={1,0,0,0,0,0,0,0};
static int ST_SagaFirst(int saga){int i;for(i=0;i<ST_COUNT;i++)if(stChapters[i].saga==saga)return i;return -1;}
static int ST_SagaChapterCount(int saga){int i,n=0;for(i=0;i<ST_COUNT;i++)if(stChapters[i].saga==saga)n++;return n;}
static int ST_SagaCanOpen(int saga,unsigned completed){unsigned need;if(saga<0||saga>=ST_SAGA_COUNT)return 0;need=(1u<<saga)-1u;return (completed&need)==need;}
static int ST_SagaCleared(int saga,int finalized,const unsigned*progress){int i,n=0;if(!finalized||!progress)return 0;for(i=0;i<ST_COUNT;i++)if(stChapters[i].saga==saga){n++;if(!progress[i])return 0;}return n>0;}
static int ST_SagaNextChapter(int chapter,int delta){int i,best=-1,wrap=-1,seq;if(chapter<0||chapter>=ST_COUNT)return 0;seq=stChapters[chapter].sequence;for(i=0;i<ST_COUNT;i++)if(stChapters[i].saga==stChapters[chapter].saga){int n=stChapters[i].sequence;if(wrap<0||(delta>0?n<stChapters[wrap].sequence:n>stChapters[wrap].sequence))wrap=i;if((delta>0?n>seq:n<seq)&&(best<0||(delta>0?n<stChapters[best].sequence:n>stChapters[best].sequence)))best=i;}return best>=0?best:wrap>=0?wrap:chapter;}

/* Who is in a chapter, in draft order: team 0 first (the first of them is the human), then team 1. A scene actor
   is loaded with the fight but kept out of it (parked like a fusion-consumed partner, mod_story_runtime.inc) and
   appears only in the chapter's cutscenes. Speakers name actors by `name` (StLine.speaker). */
#define ST_ACTORS 16 /* = MS_MAX: a chapter's whole draft, scene-only actors included */
#define ST_ACTOR_SCENE 1
/* With ST_ACTOR_SCENE: a fighter waiting on the sidelines (shown in cutscenes, parked otherwise) until a story event
   brings him into the fight (StEvent.enterMask); an event can send a fighter back to the sidelines (exitMask). */
#define ST_ACTOR_RESERVE 2
/* Only in the Full story battle density (setting story_density_full=1): left out of the draft otherwise. */
#define ST_ACTOR_DENSE 4
typedef struct StActor {const char *name;int chara,costume,team,flags;} StActor;
typedef struct StCast {
 int count;StActor actor[ST_ACTORS];
 int carried,carrier;          /* canonical ending: cast index of the actor carried off, and of the one carrying (-1 none) */
 int bridgeCount;StLine bridge[2]; /* canonical ending, after the departure: an arrival and its lines */
 int arriving;                 /* cast index of the actor who arrives for the bridge (-1 none) */
 const char *leavesCanon[2];   /* caption of the canonical departure */
 int noIntroCompanion;         /* 1: the carried actor is not beside the lead in the opening (chapter 3: Gohan is a prisoner) */
 int noLeaves;                 /* 1: no departure caption / flight after the endings (chapter 3: Raditz does not leave) */
 int canonLeadDown;            /* 1: the human lead lies defeated in the canonical ending (chapter 3: Goku's sacrifice) */
 unsigned introShow,canonShow,altShow; /* scene-only cast entries (bit = cast index) shown in the opening / endings */
 unsigned showDown;            /* of those, the ones shown lying down (hurt, asleep) */
 unsigned endDown;             /* of those, the ones shown lying down in the endings only (asleep after the fight) */
 int canonRivalUp;             /* 1: the rival is still standing in the canonical ending (the story beat ended it, not a KO) */
 unsigned endHide;             /* cast entries hidden in the opening and both endings (a Great Ape before / after) */
 unsigned departMask;          /* cast entries who fly off together in the departure shot (instead of the rival alone) */
} StCast;
static const StCast stCasts[ST_COUNT]={
 /* costumes: Piccolo 1 = weighted cape (322), Raditz 0 (1,200); chapter 2: Goku 2 = Kame House (334; costume 3 = 416
    is kept for chapter 3), Krillin 1 (1,770, his lowest), kid Gohan 3 = yellow outfit, Piccolo 1. */
 {2,{{"Piccolo",22,1,0,0},{"Raditz",77,0,1,0}},-1,-1,0,{{0,{0,0}}},-1,{"Raditz leaves.","Raditz se marcha."}},
 {5,{{"Goku",0,2,0,0},{"Krillin",25,1,0,0},{"Raditz",77,0,1,0},{"Gohan",13,3,1,ST_ACTOR_SCENE},{"Piccolo",22,1,1,ST_ACTOR_SCENE}},
  3,2,2,
  {{"Piccolo",{"For once, your problem is mine too. We'll need two against one.","Por una vez, tu problema tambi\303\251n es el m\303\255o. Vamos a necesitar dos contra uno."}}, /* SS02_013 */
   {"Goku",{"We get Gohan back first. Everything else can wait.","Recuperamos a Gohan primero. Lo dem\303\241s puede esperar."}}}, /* SS02_014 */
  4,
  /* DRAFT caption */
  {"Raditz takes Gohan away.","Raditz se lleva a Gohan."}},
 /* Chapter 3: Goku (costume 3, label 416), unweighted Piccolo (408) vs Raditz; kid Gohan (yellow) is a scene actor for the R3
    intervention and the alternative ending. Canonical = a PLAYER win after the Special Beam Cannon (ST_CanonIsWin). */
 {4,{{"Goku",0,3,0,0},{"Piccolo",22,0,0,0},{"Raditz",77,0,1,0},{"Gohan",13,3,1,ST_ACTOR_SCENE}},
  3,-1,0,{{0,{0,0}}},-1,{0,0},1,1,1},
 /* chapter 4: Gohan fights as himself and turns into the Great Ape (180) through an authored native form change; the
    endings show him asleep as a child (scene-only copy, Piccolo's side so its draft key differs) */
 {3,{{"Piccolo",22,1,0,0},{"Gohan",13,0,1,0},{"Sleeping Gohan",13,0,0,ST_ACTOR_SCENE}},
  -1,-1,0,{{0,{0,0}}},-1,{0,0},1,1,0,0x0u,0x4u,0x4u,0x0u,0x4u,0,0x2u} /* saiyan_04_gohans_power */,
 {2,{{"Gohan",13,0,0,0},{"Piccolo",22,1,1,0}},
  -1,-1,0,{{0,{0,0}}},-1,{0,0},1,1,0,0x0u,0x0u,0x0u,0x0u,0x0u,0,0x0u} /* saiyan_05_one_year */,
 /* chapter 6: the final trial of the year (handoff 12): five heroes, Yajirobe (301) included, against five doubles */
 {13,{{"Krillin",25,0,0,0},{"Yamcha",26,1,0,0},{"Tenshinhan",27,1,0,0},{"Chaozu",28,1,0,0},{"Yajirobe",301,0,0,0},{"Krillin's double",25,0,1,0},{"Yamcha's double",26,1,1,0},{"Tien's double",27,1,1,0},{"Chiaotzu's double",28,1,1,0},{"Yajirobe's double",301,0,1,0},{"Roshi",66,0,0,ST_ACTOR_SCENE},{"Kami",280,0,0,ST_ACTOR_SCENE},{"Mr. Popo",187,0,0,ST_ACTOR_SCENE}},
  -1,-1,0,{{0,{0,0}}},-1,{0,0},1,1,0,0x1800u,0x1800u,0x1800u,0x0u,0x0u,0,0x0u} /* saiyan_06_lookout */,
 /* chapter 7: one continuous battle (old 7-11). Tien opens; Yamcha, Krillin, Piccolo, Gohan, Chiaotzu, Goku and Nappa
    wait on the sidelines until their phase; Vegeta only watches. One Saibaman slot serves every wave until the
    16-fighter capacity step. */
 {15,{{"Tenshinhan",27,1,0,0},{"Yamcha",26,1,0,ST_ACTOR_SCENE|ST_ACTOR_RESERVE},{"Krillin",25,0,0,ST_ACTOR_SCENE|ST_ACTOR_RESERVE},{"Piccolo",22,0,0,ST_ACTOR_SCENE|ST_ACTOR_RESERVE},{"Gohan",13,0,0,ST_ACTOR_SCENE|ST_ACTOR_RESERVE},{"Chaozu",28,1,0,ST_ACTOR_SCENE|ST_ACTOR_RESERVE},{"Goku",0,0,0,ST_ACTOR_SCENE|ST_ACTOR_RESERVE},{"Saibaman",79,0,1,0},{"Nappa",80,0,1,ST_ACTOR_SCENE|ST_ACTOR_RESERVE},{"Vegeta",29,0,1,ST_ACTOR_SCENE},
  {"Saibaman 2",79,0,1,ST_ACTOR_SCENE|ST_ACTOR_RESERVE},{"Saibaman 3",79,0,1,ST_ACTOR_SCENE|ST_ACTOR_RESERVE},{"Saibaman 4",79,0,1,ST_ACTOR_SCENE|ST_ACTOR_RESERVE},{"Yamcha Saibaman",79,0,1,ST_ACTOR_SCENE|ST_ACTOR_RESERVE},{"Saibaman 5",79,0,1,ST_ACTOR_SCENE|ST_ACTOR_RESERVE}},
  -1,-1,0,{{0,{0,0}}},-1,{"Goku and Vegeta leave for a place where no one else will get caught in their fight.","Goku y Vegeta se alejan hacia un lugar donde nadie m\303\241s quede atrapado en su pelea."},1,0,0,0x33eu,0x214u,0x0u,0x0u,0x0u,0,0x0u,0x240u} /* Goku and Vegeta leave together (DRAFT caption) */ /* saiyan_07_earths_last_defense */,
 {7,{{"Yamcha",26,1,0,0},{"Krillin",25,0,0,ST_ACTOR_SCENE},{"Piccolo",22,1,0,ST_ACTOR_SCENE},{"Gohan",13,0,0,ST_ACTOR_SCENE},{"Saibaman",79,0,1,0},{"Vegeta",29,0,1,ST_ACTOR_SCENE},{"Nappa",80,0,1,ST_ACTOR_SCENE}},
  -1,-1,0,{{0,{0,0}}},-1,{0,0},1,1,0,0x6eu,0xeu,0x2u,0x0u,0x0u,0,0x0u} /* saiyan_08_six_seeds */,
 {7,{{"Tenshinhan",27,0,0,0},{"Chaozu",28,0,0,0},{"Krillin",25,0,0,0},{"Piccolo",22,1,0,0},{"Gohan",13,0,0,0},{"Nappa",80,0,1,0},{"Vegeta",29,0,1,ST_ACTOR_SCENE}},
  -1,-1,0,{{0,{0,0}}},-1,{0,0},1,1,0,0x40u,0x0u,0x0u,0x0u,0x0u,1,0x0u} /* saiyan_09_nappa */,
 {5,{{"Piccolo",22,1,0,0},{"Krillin",25,0,0,0},{"Gohan",13,0,0,0},{"Nappa",80,0,1,0},{"Vegeta",29,0,1,ST_ACTOR_SCENE}},
  -1,-1,0,{{0,{0,0}}},-1,{0,0},1,1,0,0x10u,0x0u,0x0u,0x0u,0x0u,1,0x0u} /* saiyan_10_three_hours */,
 {5,{{"Goku",0,0,0,0},{"Gohan",13,0,0,ST_ACTOR_SCENE},{"Krillin",25,0,0,ST_ACTOR_SCENE},{"Nappa",80,0,1,0},{"Vegeta",29,0,1,ST_ACTOR_SCENE}},
  -1,-1,0,{{0,{0,0}}},-1,{0,0},1,1,0,0x16u,0x10u,0x0u,0x0u,0x0u,0,0x0u} /* saiyan_11_goku_returns */,
 /* chapter 8: one battle on the same rocky field (old 12-17). Goku vs Vegeta first; Gohan and Krillin join when Goku is
    crushed; Yajirobe stays out of the giant's attention (cutscenes). The Great Apes are native transformations of the
    same fighters (Vegeta 29->30, Gohan 13->180), Kaioken is Goku's native form 231. */
 {5,{{"Goku",0,0,0,0},{"Gohan",13,0,0,ST_ACTOR_SCENE|ST_ACTOR_RESERVE},{"Krillin",25,0,0,ST_ACTOR_SCENE|ST_ACTOR_RESERVE},{"Yajirobe",301,0,0,ST_ACTOR_SCENE},{"Vegeta",29,0,1,0}}, /* Krilin borrows the existing Goku's technique; no sixth fighter. */
  -1,-1,0,{{0,{0,0}}},-1,{0,0},1,1,0,0x0u,0xeu,0x0u,0x0u,0x0u,0,0x0u} /* saiyan_12_battle_for_earth */,
 {2,{{"Goku",0,0,0,0},{"Vegeta",30,0,1,0}},
  -1,-1,0,{{0,{0,0}}},-1,{0,0},1,1,1,0x0u,0x0u,0x0u,0x0u,0x0u,1,0x0u} /* saiyan_13_artificial_moon */,
 {5,{{"Gohan",13,0,0,0},{"Krillin",25,0,0,0},{"Goku",0,0,0,ST_ACTOR_SCENE},{"Yajirobe",301,0,0,ST_ACTOR_SCENE},{"Vegeta",30,0,1,0}},
  -1,-1,0,{{0,{0,0}}},-1,{0,0},1,1,0,0xcu,0xcu,0x4u,0x4u,0x0u,1,0x0u} /* saiyan_14_yajirobe */,
 {4,{{"Gohan",13,0,0,0},{"Krillin",25,0,0,ST_ACTOR_SCENE},{"Goku",0,0,0,ST_ACTOR_SCENE},{"Vegeta",29,0,1,0}},
  -1,-1,0,{{0,{0,0}}},-1,{0,0},1,1,0,0x6u,0x2u,0x2u,0x4u,0x0u,1,0x0u} /* saiyan_15_last_energy */,
 {5,{{"Krillin",25,0,0,0},{"Vegeta",29,0,1,0},{"Great Ape Gohan",180,0,1,0},{"Goku",0,0,0,ST_ACTOR_SCENE},{"Gohan",13,0,0,ST_ACTOR_SCENE}},
  -1,-1,0,{{0,{0,0}}},-1,{0,0},1,1,0,0x8u,0x10u,0x10u,0x18u,0x0u,0,0x4u} /* saiyan_16_last_giant */,
 {4,{{"Krillin",25,0,0,0},{"Vegeta",29,0,1,0},{"Goku",0,0,0,ST_ACTOR_SCENE},{"Gohan",13,0,0,ST_ACTOR_SCENE}},
  -1,-1,0,{{0,{0,0}}},-1,{0,0},1,1,0,0x4u,0xcu,0x0u,0x4u,0x0u,1,0x0u} /* saiyan_17_let_him_live */,
 {5,{{"Krillin",25,0,0,0},{"Yamcha",26,1,0,0},{"Tenshinhan",27,1,1,0},{"Chaozu",28,1,1,0},{"Yajirobe",301,0,1,0}},
  -1,-1,0,{{0,{0,0}}},-1,{0,0},1,1,0,0x0u,0x0u,0x0u,0x0u,0x0u,0,0x0u,0x0u} /* saiyan_05b_lookout_training: sparring pairs */,
 {2,{{"Goku",0,0,0,0},{"King Kai",208,0,1,0}},
  -1,-1,0,{{0,{0,0}}},-1,{0,0},1,1,0,0x0u,0x0u,0x0u,0x0u,0x0u,0,0x0u,0x0u} /* saiyan_05c_king_kai */,
 {2,{{"Roshi",66,0,0,0},{"Offscreen",0,0,1,ST_ACTOR_SCENE}},-1,-1,0,{{0}},-1,{0,0},1,1},
 {2,{{"Goku",0,0,0,0},{"King Kai",208,0,1,0}},-1,-1,0,{{0}},-1,{0,0},1,1}

,
 {7,{{"Krillin",25,0,0,0},{"Yamcha",26,1,0,0},{"Tenshinhan",27,1,0,0},{"Chaozu",28,1,0,0},{"Yajirobe",301,0,0,0},{"Shorty",323,0,1,0},{"Scarface",324,0,1,0}},
 -1,-1,0,{{0}},-1,{0,0},1,1}
};
/* Chapter presentation uses the story-era reading, not a borrowed costume's label.
   Cast order matches stCasts; -1 is scene-only/not displayed. Raditz: manga reading1,200.
   Krillin206 is Bulma's early-Z scouter reading (manga ch209); Goku/Piccolo
   weighted334/322, unweighted416/408 (manga ch195/199). No gameplay stat changes. */
static const int stChapterPower[ST_COUNT][ST_ACTORS]={
 {322,1500,-1,-1,-1},
 {334,206,1500,-1,-1},
 {416,408,1500,-1,-1},
 /* chapters 4-17 (SAIYAN-SAGA-CONSOLIDATION-HANDOFF section 6, guide figures marked as such): Raditz 1,500 per
    Daizenshuu 7; Piccolo 408 right after Raditz, 3,500 guide maximum after the year; Gohan 981 after training; Krillin
    1,770 / Yamcha 1,480 / Tien 1,830 / Chiaotzu 610 guide maxima; Saibaman 1,200; Nappa 4,000; Goku 8,000 nominal;
    Vegeta 18,000; Vegeta Great Ape 180,000 (x10); Yajirobe 970 (Juan approved). -1: no fixed reading (Great Ape Gohan). */
 {408,-1,-1,-1,-1,-1,-1,-1,-1,-1},
 {981,3500,-1,-1,-1,-1,-1,-1,-1,-1},
 {1770,1480,1830,610,970,1770,1480,1830,610,970}, /* Yajirobe 970 (Juan approved) */
 {1830,1480,1770,3500,981,610,8000,1200,4000,18000,1200,1200,1200,1200,1200},
 {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
 {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
 {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
 {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
 {8000,981,1770,970,18000,-1,-1,-1,-1,-1},
 {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
 {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
 {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
 {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
 {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
 {1770,1480,1830,610,970,-1,-1,-1,-1,-1},
 {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}

,
{0},
{0},
 {1770,1480,1830,610,970,800,800}
};
/* Juan 2026-11-05: in these chapters the listed allies' melee hits (punches, kicks, rush and smash blows) still deal
   their damage but never make the rival react (native reaction 2, "no flinch"). Bits are stCasts indices, so a
   form change (Great Ape Gohan/Vegeta), a costume reload or a team handover keeps the rule. Chapter 6 (the Lookout)
   is deliberately absent. CH7/CH8: every ally except Goku (cast 6 / cast 0), whose blows keep their native reaction.
   Ki blasts, supers, throws and authored scene attacks are not covered (mod_story_scene.inc Port_StoryPunchArmor). */
typedef struct StPunchArmor {int chapter;unsigned attackers,rivals;} StPunchArmor;
static const StPunchArmor stPunchArmor[]={
 {0,0x1u,0x2u},    /* CH1 Piccolo -> Raditz */
 {1,0x3u,0x4u},    /* CH2 Goku, Krillin -> Raditz */
 {2,0x3u,0x4u},    /* CH3 Goku, Piccolo -> Raditz */
 {3,0x1u,0x2u},    /* CH4 Piccolo -> Gohan, also as the Great Ape */
 {4,0x1u,0x2u},    /* CH5 Gohan -> Piccolo */
 {6,0x3fu,0x100u}, /* CH7 Tien, Yamcha, Krillin, Piccolo, Gohan, Chiaotzu (not Goku) -> Nappa */
 {11,0xeu,0x10u}  /* CH8 Gohan, Krillin, Yajirobe -> Vegeta, also as the Great Ape */
};
/* Juan: in CH8 the Great Ape Gohan (form 180) DOES make Vegeta flinch; Krillin and the others never do. */
static int ST_PunchArmorForm(int chapter,int byChara){return !(chapter==11&&byChara==180);}
static int ST_PunchArmor(int chapter,int byCast,int atCast){
 unsigned i;
 if(byCast<0||byCast>=ST_ACTORS||atCast<0||atCast>=ST_ACTORS)return 0;
 for(i=0;i<sizeof(stPunchArmor)/sizeof(stPunchArmor[0]);i++)
  if(stPunchArmor[i].chapter==chapter)return (stPunchArmor[i].attackers>>byCast&1u)&&(stPunchArmor[i].rivals>>atCast&1u);
 return 0;
}
/* SSF_001, SSF_003: shown only for a genuine failure (the fight was left without a result). */
static const char *const stFailText[2][2]={{"Earth still needs you. Try again.","La Tierra a\303\272n te necesita. Vuelve a intentarlo."},{"Retry chapter.","Reintentar el cap\303\255tulo."}};
static const char *const stLeaves[2]={"Raditz leaves.","Raditz se marcha."};
/* In-engine presentation (port/include/mod_story_scene.inc, drawn by mod_story_subtitle.inc).
   The opening plays every intro line; the canonical ending plays canon lines from canonFirst (SS01_004 is said
   during the fight instead); each ending closes on the stLeaves caption. Cue lines are said once while the
   fight goes on: a time in seconds, or a fighter's health at or below a percentage. */
enum { ST_SET_INTRO, ST_SET_CANON, ST_SET_ALT, ST_SET_COMBAT, ST_SET_LEAVES, ST_SET_BRIDGE, ST_SET_LEAVES_ALT,
       ST_SET_EVENT0 }; /* then one set per authored event: ST_SET_EVENT0 + k, k < ST_EVENTS */
#define ST_SET_YAMCHA_ALIVE 100
#define ST_SET_EVENT1 (ST_SET_EVENT0 + 1)
/* An event's alternative lines (StEvent.alt): ST_SET_ALTEV0 + k. */
#define ST_SET_ALTEV0 (ST_SET_EVENT0 + ST_EVENTS_MAX_SETS)
#define ST_EVENTS_MAX_SETS 16
#define ST_SET_EVENT2 (ST_SET_EVENT0 + 2)
enum { ST_CUE_TIME, ST_CUE_RIVAL_HP, ST_CUE_PLAYER_HP };
#define ST_CUES 8
typedef struct StCue {int kind,value;StLine line;} StCue;
typedef struct StScene {const char *rivalSpeaker;int canonFirst,cueCount;StCue cue[ST_CUES];} StScene;
static const StScene stScenes[ST_COUNT]={
 {"Raditz",1,4,{
  /* DRAFT, pending Juan's approval: not in SAIYAN-SAGA-SCRIPT-ES-EN.md */
  {ST_CUE_TIME,6,{"Raditz",{"You're wasting my time. I'm looking for Kakarot.","No me hagas perder el tiempo. Estoy buscando a Kakarotto."}}},
  /* SS01_004 (script of record) */
  {ST_CUE_RIVAL_HP,60,{"Piccolo",{"I hit him clean... and he barely moved.","\302\241Le di de lleno... y apenas retrocedi\303\263!"}}},
  /* DRAFT */
  {ST_CUE_PLAYER_HP,50,{"Raditz",{"You can't stop me with attacks like that.","No vas a detenerme con ataques as\303\255."}}},
  /* DRAFT */
  {ST_CUE_RIVAL_HP,30,{"Piccolo",{"I only need one opening. I'll find it.","Solo necesito una abertura. La encontrar\303\251."}}}}},
 {"Raditz",0,4,{
  /* DRAFTS, pending Juan's approval: the script has no combat lines for node 02 */
  {ST_CUE_TIME,5,{"Raditz",{"You brought a friend, Kakarot? It won't help.","\302\277Trajiste a un amigo, Kakarotto? No les servir\303\241."}}},
  {ST_CUE_RIVAL_HP,60,{"Krillin",{"Goku, we need to catch his tail!","\302\241Goku, tenemos que agarrarle la cola!"}}},
  {ST_CUE_PLAYER_HP,50,{"Raditz",{"You would rather die than stand with your own people?","\302\277Prefieres morir antes que unirte a los tuyos?"}}},
  {ST_CUE_RIVAL_HP,30,{"Goku",{"Krillin, now! Don't let him get away!","\302\241Krilin, ahora! No dejes que escape!"}}}}},
 {"Raditz",0,3,{
  /* script of record SS03_004-006 */
  {ST_CUE_TIME,8,{"Piccolo",{"His armor is tough. Keep him busy while I line up the shot.","Su armadura es resistente. Distr\303\241elo mientras apunto."}}},
  {ST_CUE_TIME,16,{"Goku",{"I'll hold him still. Take your shot when you're ready.","Yo lo sujetar\303\251. Dispara cuando tengas una oportunidad."}}},
  {ST_CUE_RIVAL_HP,70,{"Raditz",{"You think I'll let you aim that attack at me?","\302\277Crees que voy a dejar que apuntes ese ataque contra m\303\255?"}}},}},
 {"Gohan",0,1,{
  {ST_CUE_TIME,12,{"Piccolo",{"Gohan, come back to your senses!","\302\241Gohan, vuelve en ti!"}} /* SS05_006 */}}},
 {"Piccolo",0,2,{
  {ST_CUE_TIME,6,{"Piccolo",{"Good. Now dodge the next one.","Bien. Ahora esquiva el siguiente."}} /* SS06_003 */},
  {ST_CUE_RIVAL_HP,60,{"Gohan",{"I did it!","\302\241Lo logr\303\251!"}} /* SS06_004 */}}},
 {"Krillin's double",0,0,{{0}}},
 {"Nappa",0,4,{
  {ST_CUE_TIME,1,{"Tenshinhan",{"I'll take the first one!","\302\241Yo me encargo del primero!"}} /* SS08_001 */},
  {ST_CUE_RIVAL_HP,90,{"Chaozu",{"Tien, I can't hold him much longer!","\302\241Tenshinhan, no podr\303\251 aguantarlo mucho m\303\241s!"}} /* SS09_004 */},
  {ST_CUE_RIVAL_HP,85,{"Piccolo",{"Gohan, stay behind me!","\302\241Gohan, qu\303\251date detr\303\241s de m\303\255!"}} /* SS09_005 */},
  {ST_CUE_RIVAL_HP,45,{"Gohan",{"Dad made it! He's here!","\302\241Pap\303\241 lleg\303\263! \302\241Est\303\241 aqu\303\255!"}} /* SS10_010 */}}},
 {"Saibaman",0,0,{{0}}},
 {"Nappa",0,2,{
  {ST_CUE_RIVAL_HP,90,{"Chaozu",{"Tien, I can't hold him much longer!","\302\241Tenshinhan, no podr\303\251 aguantarlo mucho m\303\241s!"}} /* SS09_004 */},
  {ST_CUE_RIVAL_HP,85,{"Piccolo",{"Gohan, stay behind me!","\302\241Gohan, qu\303\251date detr\303\241s de m\303\255!"}} /* SS09_005 */}}},
 {"Nappa",0,2,{
  {ST_CUE_RIVAL_HP,55,{"Gohan",{"Dad made it! He's here!","\302\241Pap\303\241 lleg\303\263! \302\241Est\303\241 aqu\303\255!"}} /* SS10_010 */},
  {ST_CUE_RIVAL_HP,50,{"Nappa",{"He won't make it in time.","No llegar\303\241 a tiempo."}} /* SS10_011 */}}},
 {"Nappa",0,0,{{0}}},
 {"Vegeta",0,0,{{0}}},
 {"Vegeta",0,0,{{0}}},
 {"Vegeta",0,0,{{0}}},
 {"Vegeta",0,0,{{0}}},
 {"Vegeta",0,0,{{0}}},
 {"Vegeta",0,0,{{0}}},
 {"Tenshinhan",0,0,{{0}}},
 {"King Kai",0,0,{{0}}}

,
{0},
{0},
 {"Shorty",0,3,{
 {ST_CUE_TIME,8,{"Shorty",{"You call that a team? You're getting in each other's way!","\302\277Eso es un equipo? \302\241No hacen m\303\241s que estorbarse!"}}},
 {ST_CUE_RIVAL_HP,50,{"Tenshinhan",{"They can be hurt. Keep the pressure on together!","Podemos herirlos. \302\241Mantengamos la presi\303\263n entre todos!"}}},
 {ST_CUE_PLAYER_HP,40,{"Yamcha",{"Don't let them separate us! Cover Krillin!","\302\241No dejen que nos separen! \302\241Cubran a Krilin!"}}}}}
};
/* Authored R3 events (chapter 3): offered when the rival's HP falls to an ABSOLUTE value (no difficulty scaling), in
   story order, each once; accepted with R3 (battle event 0x50) when the fighters can be interrupted. Juan: the prompt
   is the R3 glyph of the native HUD cue only, with no caption text. */
#define ST_EVENTS 16
#define ST_EVENT_LINES 6
/* GOHAN / BEAM: chapter 3's authored set pieces. SCENE: a generic story event - its lines, then its effects. */
enum { ST_EVENT_GOHAN = 1, ST_EVENT_BEAM = 2, ST_EVENT_SCENE = 3, ST_EVENT_MOON = 4 };
/* MOON (chapter 4): the lead charges during the lines (stage dim, aura, hand glow, charge loop), then in the fight he
   fires into the sky (the moon: flash, impact sound), then a native beam at the rival (the tail); when it lands, or after
   ST_MOON_BEAM_FRAMES, the chapter ends canonically. */
/* When an event is offered: the rival's absolute HP at or below `threshold` (chapter 3), the rival's HP percentage,
   or the fight's elapsed seconds. */
enum { ST_TRIG_RIVAL_HP, ST_TRIG_RIVAL_PCT, ST_TRIG_TIME, ST_TRIG_AFTER };
/* ST_TRIG_AFTER: `threshold` seconds after the previous event of the chapter ended (a phased chapter's AUTO beat). */
/* switchCast: the cast entry that changes sides for the event's attack (joins the rival's team: the attack then targets
   him too), -1 none. Reusable by any later chapter's event. */
/* After a SCENE event's lines: `damage` absolute HP off the rival (never below 1); killMask cast entries knocked out and
   shown down in the ending; removeMask cast entries leave the fight for good (a story death: parked, never revived);
   showMask scene-only cast shown during it; canon 1: completing it is the canonical clear (the fight ends, the player's
   side recorded the winner); switchToLead 1: switchCast joins the lead's team instead of the rival's; autoStart 1: it
   starts by itself (an AUTO beat) instead of waiting for R3. */
typedef struct StEvent {int threshold,kind,lineCount,switchCast;StLine line[ST_EVENT_LINES];
 int trigger,autoStart,damage;unsigned killMask,removeMask,showMask;int canon,switchToLead;
 int transformCast,transformTo; /* transformTo > 0: after the lines that cast entry transforms into that character
                                   through the native form change (granted for this fight only, mod_scenarios_runtime) */
 unsigned enterMask,exitMask;   /* reserve fighters who enter the fight / fighters who step back to the sidelines */
 int handTo;                    /* cast index + 1 of the hero who takes the player's control (0: unchanged) */
 int refill;                    /* 1: the rival comes back at full health as the next of his kind (a new wave) */
 int koAlt;                     /* phased: the phase enemy knocked out while this event is pending - 0 the event plays,
                                   1 the phase's alternative (alt lines, deaths / hand-over skipped, the battle goes on),
                                   2 the battle ends on the alternative (alt lines are the ending). Script section 18. */
 int altCount;StLine alt[2];
 int attackCast,attackTech,attackTarget,counterCast,counterTech,chargeCast; /* authored attack (cast index + 1; see
                                   mod_story_scene.inc st_attack_update): technique -1 = a strike / explosion */
 int checkpoint;                /* 1: a retry after a failure may resume at this phase (handoff section 7) */
 int choreo;                    /* the attack's authored choreography (ST_CHOREO_*; mod_story_scene.inc st_choreo_*) */
 int helperCast;                /* cast index + 1 of the hidden actor whose native technique a choreography uses */
} StEvent;
#define ST_CHOREO_CLING 1   /* the attacker holds on to the target's back, then his native technique fires from there */
#define ST_CHOREO_TAILCUT 2 /* a sneak stroke at the tail from behind; the event's form change happens when it lands */
#define ST_CHOREO_TRANSFER 3 /* the attacker goes to the (wounded) target's side and receives his energy */
#define ST_CHOREO_EXECUTE 5 /* a scene actor joins just long enough to fire his real technique */
#define ST_CHOREO_CLASH 6 /* the attacker and the target face off in the native trading-blows clash */
#define ST_CHOREO_SPIRITBOMB 4 /* the helper's native ball thrown from the attacker; the target dodges, Gohan returns it */
typedef struct StEvents {int count;StEvent event[ST_EVENTS];} StEvents;
static const StEvents stEvents[ST_COUNT]={
 {0},{0},
 {2,{{20000,ST_EVENT_GOHAN,4,-1,
   {{"Gohan",{"Get away from my dad!","\302\241Al\303\251jate de mi pap\303\241!"}}, /* SS03_011 */
   {"Raditz",{"What...? That power came from the boy?","\302\277Qu\303\251...? \302\277Ese poder sali\303\263 del ni\303\261o?"}}, /* SS03_012 */
   {"Goku",{"Gohan, get back to safety!","\302\241Gohan, ponte a salvo!"}}, /* SS03_013 */
   {"Piccolo",{"He got him! Goku, don't let him recover.","\302\241Lo alcanz\303\263! Goku, no le des tiempo para recuperarse."}}, /* SS03_014 */}},
     {10000,ST_EVENT_BEAM,4,0, /* Goku switches sides for the Special Beam Cannon */
   {{"Goku",{"Piccolo, now!","\302\241Piccoro, ahora!"}}, /* SS04_001 */
   {"Piccolo",{"The shot will go through you too!","\302\241El ataque te atravesar\303\241 tambi\303\251n!"}}, /* SS04_002 */
   {"Goku",{"Do it! I'm not letting him go.","\302\241Hazlo! No voy a soltarlo."}}, /* SS04_003 */
   {"Raditz",{"Kakarot! You'll die with me!","\302\241Kakarotto! \302\241Vas a morir conmigo!"}}, /* SS04_004 */},
   ST_TRIG_RIVAL_HP,0,0,0,0,0,1}}} /* the beam is chapter 3's canonical event */
,
 /* chapter 4: the transformation (AUTO, SS05_005), then R3 destroy the moon / cut the tail (SS05_007-008) */
 {2,{{1,ST_EVENT_SCENE,1,-1,
   {{"Piccolo",{"Gohan...? The moon is making him transform!","\302\277Gohan...? \302\241La luna lo est\303\241 transformando!"}} /* SS05_005 */},
   ST_TRIG_TIME,1,0,0x0u,0x0u,0x0u,0,0,1,180},
  {45,ST_EVENT_MOON,2,-1,
   {{"Piccolo",{"I have to stop this before he hurts himself.","Tengo que detenerlo antes de que se haga da\303\261o."}} /* SS05_007 */,
   {"Piccolo",{"I won't let that tail put anyone in danger again.","No dejar\303\251 que esa cola vuelva a poner a nadie en peligro."}} /* SS05_008 */},
   ST_TRIG_RIVAL_PCT,0,0,0x0u,0x0u,0x0u,1,0}}},
 {1,{{30,ST_EVENT_SCENE,2,-1, /* chapter 5: the closing exchange, a native dash clash (ST_CHOREO_CLASH) */
   {{"Piccolo",{"Enough holding back. Show me what this year has made of you!","Basta de contenerte. \302\241Mu\303\251strame en qu\303\251 te ha convertido este a\303\261o!"}} /* DRAFT */,
   {"Gohan",{"Here I come, Mr. Piccolo!","\302\241All\303\241 voy, se\303\261or Piccoro!"}} /* DRAFT */},
   ST_TRIG_RIVAL_PCT,1,0,0x0u,0x0u,0x0u,1,0,0,0,0x0u,0x0u,0,0,0,0,{{0,{0,0}}},1,-1,2,0,0,0,0,ST_CHOREO_CLASH,0}}},
 {1,{{10,ST_EVENT_SCENE,4,-1,
   {{"Mr. Popo",{"Don't chase your double. Cover the fighter beside you.","No persigan a su doble. Protejan a quien tienen al lado."}} /* SS06_015 */,
   {"Krillin",{"Mine already knows what I'm about to do!","\302\241El m\303\255o ya sabe lo que voy a hacer!"}} /* SS06_016 */,
   {"Yamcha",{"Then let's change the rhythm.","Entonces cambiemos el ritmo."}} /* SS06_017 */,
   {"Tenshinhan",{"Together, now!","\302\241Ahora, juntos!"}} /* SS06_018 */},
   ST_TRIG_TIME,0,0,0x0u,0x0u,0x0u,0,0}}},
 /* chapter 7 phases: A Tien, B Yamcha, C Krillin/Piccolo, D Nappa (Chiaotzu, Tien), E the wait, F Piccolo protects Gohan,
    G Goku arrives, H the Kaioken interception */
 {11,{{40,ST_EVENT_SCENE,5,-1,
   {{"Vegeta",{"Stay down.","No te levantes."}} /* SS08_002 */,
   {"Krillin",{"Vegeta just killed one of his own...","Vegeta acaba de matar a uno de los suyos..."}} /* SS08_003 */,
   {"Yamcha",{"Stay back, Krillin. I've got this one.","Krilin, qu\303\251date atr\303\241s. Yo me encargo."}} /* SS08_004 */,
   {"Krillin",{"Don't let your guard down!","\302\241No te conf\303\255es!"}} /* SS08_005 */,
   {"Yamcha",{"I can handle one Saibaiman.","Puedo con un Saibaiman."}} /* SS08_006 */},
   ST_TRIG_RIVAL_PCT,0,0,0x0u,0x80u,0x206u,0,0,0,0,0x2002u,0x1u,2,0,0,0,{{0}},10,2,8,0,0,10,0,ST_CHOREO_EXECUTE},
  {25,ST_EVENT_SCENE,4,-1,
   {{"Yamcha",{"Was that all?","\302\277Eso era todo?"}} /* SS08_007 */,
   {"Krillin",{"Yamcha, let go! It's going to blow!","\302\241Yamcha, su\303\251ltalo! \302\241Va a explotar!"}} /* SS08_008 */,
   {"Krillin",{"Yamcha! Everyone, get back!","\302\241Yamcha! \302\241Al\303\251jense todos!"}} /* SS08_009 */,
   {"Piccolo",{"Gohan, behind me!","\302\241Gohan, detr\303\241s de m\303\255!"}} /* SS08_010 */},
   ST_TRIG_RIVAL_PCT,0,0,0x2u,0x2000u,0x1cu,0,0,0,0,0x5c0cu,0x0u,3,0,1,2,{{"Yamcha",{"I almost let my guard down. Thanks for the warning.","Casi me conf\303\255o. Gracias por avisarme."}} /* SSA_006 */,{"Krillin",{"Next time, we celebrate after it stops moving.","La pr\303\263xima vez, lo celebramos cuando deje de moverse."}} /* SSA_007 */},14,4,2,0,0,0,0,ST_CHOREO_CLING},
  {30,ST_EVENT_SCENE,5,-1,
   {{"Gohan",{"I didn't see it coming...","No lo vi venir..."}} /* SS08_011 */,
   {"Piccolo",{"That's why you keep your eyes on it.","Por eso no debes apartar la mirada."}} /* SS08_012 */,
   {"Nappa",{"Is that all you can do?","\302\277Eso es todo lo que pueden hacer?"}} /* SS09_001 */,
   {"Tenshinhan",{"Stay together. Don't let him split us up.","Mant\303\251nganse juntos. No dejen que nos separe."}} /* SS09_002 */,
   {"Chaozu",{"Tenshinhan!","\302\241Tenshinhan!"}} /* SS09_003 */},
   ST_TRIG_RIVAL_PCT,0,0,0x0u,0x7c80u,0x130u,0,0,0,0,0x131u,0x0u,1,0},
  {80,ST_EVENT_SCENE,4,-1,
   {{"Chaozu",{"I'm holding him, Tien. Do it now!","\302\241Lo tengo sujeto, Tenshinhan! \302\241Hazlo ahora!"}} /* SS09_006 */,
   {"Tenshinhan",{"Chiaotzu, no! Let him go!","\302\241Chaozu, no! \302\241Su\303\251ltalo!"}} /* SS09_007 */,
   {"Chaozu",{"Thank you for always standing by me.","Gracias por estar siempre a mi lado."}} /* SS09_008 */,
   {"Krillin",{"He's still standing...","Sigue en pie..."}} /* SS09_009 */},
   ST_TRIG_RIVAL_PCT,0,3000,0x0u,0x20u,0x0u,0,0,0,0,0x0u,0x0u,0,0,2,2,{{"Tenshinhan",{"We did it! Chiaotzu, stay with me.","\302\241Lo conseguimos! Chaozu, qu\303\251date conmigo."}} /* SSA_008 */,{"Nappa",{"No... I couldn't have lost to them.","No... No puedo haber perdido contra ellos."}} /* SSA_009 */},6,4,9,0,0,0,1,ST_CHOREO_CLING},
  {60,ST_EVENT_SCENE,4,-1,
   {{"Tenshinhan",{"I won't let this end here!","\302\241No dejar\303\251 que esto termine aqu\303\255!"}} /* SS09_010 */,
   {"Piccolo",{"Tien, don't use all your strength!","\302\241Tenshinhan, no uses toda tu fuerza!"}} /* SS09_011 */,
   {"Tenshinhan",{"This is for Chiaotzu... and everyone still standing!","\302\241Esto es por Chaozu... y por todos los que siguen en pie!"}} /* SS09_012 */,
   {"Nappa",{"Even that couldn't bring me down.","Ni siquiera eso pudo derribarme."}} /* SS09_013 */},
   ST_TRIG_RIVAL_PCT,0,6000,0x1u,0x0u,0x0u,0,0,0,0,0x0u,0x0u,4,0,2,2,{{"Nappa",{"No... I couldn't have lost to them.","No... No puedo haber perdido contra ellos."}} /* SSA_009 */,{"Narrator",{"Alternate victory. This outcome changes what happened in this encounter.","Victoria alternativa. Este desenlace cambia lo ocurrido en este encuentro."}} /* SSA_016 */},1,4,9,0,0,0},
  {2,ST_EVENT_SCENE,5,-1,
   {{"Krillin",{"Goku will come. We just have to hold on a little longer.","Goku vendr\303\241. Solo tenemos que resistir un poco m\303\241s."}} /* SS10_001 */,
   {"Vegeta",{"Kakarot... Very well. I'll give him three hours.","Kakarotto... Muy bien. Le dar\303\251 tres horas."}} /* SS10_002 */,
   {"Nappa",{"And if he doesn't come?","\302\277Y si no aparece?"}} /* SS10_003 */,
   {"Vegeta",{"Then finish them. I won't wait any longer.","Entonces acaba con ellos. No esperar\303\251 m\303\241s."}} /* SS10_004 */,
   {"Narrator",{"Three hours later...","Tres horas despu\303\251s..."}} /* SS10_005 */},
   ST_TRIG_AFTER,1,0,0x0u,0x0u,0x200u,0,0,0,0,0x0u,0x0u,0,0},
  {50,ST_EVENT_SCENE,4,-1,
   {{"Piccolo",{"Krillin, distract him. Gohan, wait for my signal.","Krilin, distr\303\241elo. Gohan, espera mi se\303\261al."}} /* SS10_006 */,
   {"Gohan",{"I...","Yo..."}} /* SS10_007 */,
   {"Nappa",{"You thought I'd leave my tail unguarded?","\302\277Cre\303\255an que dejar\303\255a mi cola desprotegida?"}} /* SS10_008 */,
   {"Piccolo",{"Fall back!","\302\241Retrocedan!"}} /* SS10_009 */},
   ST_TRIG_RIVAL_PCT,0,0,0x0u,0x0u,0x0u,0,0,0,0,0x0u,0x0u,0,0,2,2,{{"Gohan",{"Mr. Piccolo! We're still here!","\302\241Se\303\261or Piccoro! \302\241Seguimos aqu\303\255!"}} /* SSA_010 */,{"Piccolo",{"And we will be if we keep our guard up.","Y seguiremos si no bajamos la guardia."}} /* SSA_011 */}},
  {40,ST_EVENT_SCENE,5,-1,
   {{"Gohan",{"Mr. Piccolo! Why...?","\302\241Se\303\261or Piccoro! \302\277Por qu\303\251...?"}} /* SS10_012 */,
   {"Piccolo",{"Because I wasn't going to let it hit you.","Porque no iba a dejar que te alcanzara."}} /* SS10_013 */,
   {"Gohan",{"Don't go...","No se vaya..."}} /* SS10_014 */,
   {"Piccolo",{"Keep going, Gohan.","Sigue adelante, Gohan."}} /* SS10_015 */,
   {"Gohan",{"Mr. Piccolo!","\302\241Se\303\261or Piccoro!"}} /* SS10_016 */},
   ST_TRIG_RIVAL_PCT,0,0,0x8u,0x0u,0x0u,0,0,0,0,0x0u,0x0u,5,0,2,2,{{"Gohan",{"Mr. Piccolo! We're still here!","\302\241Se\303\261or Piccoro! \302\241Seguimos aqu\303\255!"}} /* SSA_010 */,{"Piccolo",{"And we will be if we keep our guard up.","Y seguiremos si no bajamos la guardia."}} /* SSA_011 */},9,2,5,0,0,0},
  {2,ST_EVENT_SCENE,6,-1,
   {{"Krillin",{"Without Piccolo, the Dragon Balls are gone too.","Sin Piccoro, las Esferas del Drag\303\263n tambi\303\251n desaparecen."}} /* SS10_017 */,
   {"Gohan",{"I'll never forgive you!","\302\241No te lo perdonar\303\251!"}} /* SS10_018 */,
   {"Goku",{"Gohan... Krillin... tell me who else fell.","Gohan... Krilin... d\303\255ganme qui\303\251nes m\303\241s cayeron."}} /* SS11_001 */,
   {"Krillin",{"Yamcha, Tien, Chiaotzu... Piccolo saved Gohan.","Yamcha, Tenshinhan, Chaozu... Piccoro salv\303\263 a Gohan."}} /* SS11_002 */,
   {"Goku",{"I'm sorry. I'm here now.","Perd\303\263nenme. Ya estoy aqu\303\255."}} /* SS11_003 */,
   {"Nappa",{"So you're Kakarot. Let's see what you can do.","As\303\255 que t\303\272 eres Kakarotto. Veamos qu\303\251 sabes hacer."}} /* SS11_004 */},
   ST_TRIG_AFTER,1,0,0x0u,0x0u,0x0u,0,0,0,0,0x40u,0x14u,7,0,0,0,{{0,{0,0}}},5,4,9,0,0,5,1},
  {25,ST_EVENT_SCENE,2,-1,
   {{"Vegeta",{"Nappa, stop! You're no match for him!","\302\241Nappa, detente! \302\241No puedes con \303\251l!"}} /* SS11_005 */,
   {"Nappa",{"Then I'll finish those two first!","\302\241Entonces acabar\303\251 primero con esos dos!"}} /* SS11_006 */},
   ST_TRIG_RIVAL_PCT,0,0,0x0u,0x0u,0x214u,0,0,6,231,0x0u,0x0u,0,0},
  {2,ST_EVENT_SCENE,1,-1,
   {{"Goku",{"You won't touch them! Kaioken!","\302\241No les pondr\303\241s una mano encima! \302\241Kaioken!"}} /* SS11_007 */},
   ST_TRIG_AFTER,1,1000000,0x0u,0x0u,0x0u,1,0,0,0,0x0u,0x0u,0,0,0,0,{{0,{0,0}}},7,3,9,0,0,0}}},
 {1,{{25,ST_EVENT_SCENE,4,-1,
   {{"Yamcha",{"Was that all?","\302\277Eso era todo?"}} /* SS08_007 */,
   {"Krillin",{"Yamcha, let go! It's going to blow!","\302\241Yamcha, su\303\251ltalo! \302\241Va a explotar!"}} /* SS08_008 */,
   {"Krillin",{"Yamcha! Everyone, get back!","\302\241Yamcha! \302\241Al\303\251jense todos!"}} /* SS08_009 */,
   {"Piccolo",{"Gohan, behind me!","\302\241Gohan, detr\303\241s de m\303\255!"}} /* SS08_010 */},
   ST_TRIG_RIVAL_PCT,0,0,0x1u,0x10u,0xeu,1,0}}},
 {2,{{75,ST_EVENT_SCENE,4,-1,
   {{"Chaozu",{"I'm holding him, Tien. Do it now!","\302\241Lo tengo sujeto, Tenshinhan! \302\241Hazlo ahora!"}} /* SS09_006 */,
   {"Tenshinhan",{"Chiaotzu, no! Let him go!","\302\241Chaozu, no! \302\241Su\303\251ltalo!"}} /* SS09_007 */,
   {"Chaozu",{"Thank you for always standing by me.","Gracias por estar siempre a mi lado."}} /* SS09_008 */,
   {"Krillin",{"He's still standing...","Sigue en pie..."}} /* SS09_009 */},
   ST_TRIG_RIVAL_PCT,0,8000,0x0u,0x2u,0x0u,0,0},
  {50,ST_EVENT_SCENE,3,-1,
   {{"Tenshinhan",{"I won't let this end here!","\302\241No dejar\303\251 que esto termine aqu\303\255!"}} /* SS09_010 */,
   {"Piccolo",{"Tien, don't use all your strength!","\302\241Tenshinhan, no uses toda tu fuerza!"}} /* SS09_011 */,
   {"Tenshinhan",{"This is for Chiaotzu... and everyone still standing!","\302\241Esto es por Chaozu... y por todos los que siguen en pie!"}} /* SS09_012 */},
   ST_TRIG_RIVAL_PCT,0,6000,0x1u,0x0u,0x0u,1,0}}},
 {2,{{80,ST_EVENT_SCENE,4,-1,
   {{"Piccolo",{"Krillin, distract him. Gohan, wait for my signal.","Krilin, distr\303\241elo. Gohan, espera mi se\303\261al."}} /* SS10_006 */,
   {"Gohan",{"I...","Yo..."}} /* SS10_007 */,
   {"Nappa",{"You thought I'd leave my tail unguarded?","\302\277Cre\303\255an que dejar\303\255a mi cola desprotegida?"}} /* SS10_008 */,
   {"Piccolo",{"Fall back!","\302\241Retrocedan!"}} /* SS10_009 */},
   ST_TRIG_RIVAL_PCT,0,0,0x0u,0x0u,0x0u,0,0},
  {45,ST_EVENT_SCENE,5,-1,
   {{"Gohan",{"Mr. Piccolo! Why...?","\302\241Se\303\261or Piccoro! \302\277Por qu\303\251...?"}} /* SS10_012 */,
   {"Piccolo",{"Because I wasn't going to let it hit you.","Porque no iba a dejar que te alcanzara."}} /* SS10_013 */,
   {"Gohan",{"Don't go...","No se vaya..."}} /* SS10_014 */,
   {"Piccolo",{"Keep going, Gohan.","Sigue adelante, Gohan."}} /* SS10_015 */,
   {"Gohan",{"Mr. Piccolo!","\302\241Se\303\261or Piccoro!"}} /* SS10_016 */},
   ST_TRIG_RIVAL_PCT,0,0,0x1u,0x0u,0x0u,1,0}}},
 {1,{{40,ST_EVENT_SCENE,3,-1,
   {{"Vegeta",{"Get up, Nappa.","Lev\303\241ntate, Nappa."}} /* SS11_005 */,
   {"Nappa",{"Vegeta... help me.","Vegeta... ay\303\272dame."}} /* SS11_006 */,
   {"Goku",{"He was your partner.","Era tu compa\303\261ero."}} /* SS11_007 */},
   ST_TRIG_RIVAL_PCT,0,1000000,0x0u,0x0u,0x16u,1,0}}},
 /* chapter 8 phases: A Kaioken, B the clash, C the artificial moon, D the crushed Goku, E Yajirobe's cut, F the energy and
    the rebound, G the last giant (hostile, then turned on Vegeta), H the fall, I mercy */
 {16,{{80,ST_EVENT_SCENE,3,-1,
   {{"Goku",{"Kaioken isn't enough. I have to push it further.","El Kaioken no basta. Tengo que llevarlo m\303\241s lejos."}} /* SS12_004 */,
   {"King Kai",{"Goku, your body can't take much more!","\302\241Goku, tu cuerpo no soportar\303\241 mucho m\303\241s!"}} /* SS12_005 */,
   {"Goku",{"I know. I have to finish this before I collapse.","Lo s\303\251. Tengo que terminar antes de caer."}} /* SS12_006 */},
   ST_TRIG_RIVAL_PCT,0,0,0x0u,0x0u,0x0u,0,0,0,231,0x0u,0x0u,0,0,2,2,{{"Goku",{"Enough, Vegeta. You can still leave.","Ya basta, Vegeta. Todav\303\255a puedes irte."}} /* SSA_012 */,{"Vegeta",{"Don't think I'll forget this humiliation.","No creas que voy a olvidar esta humillaci\303\263n."}} /* SSA_013 */}},
  {60,ST_EVENT_SCENE,3,-1,
   {{"Vegeta",{"You want to save this planet? Then stop my attack!","\302\277Quieres salvar este planeta? \302\241Entonces det\303\251n mi ataque!"}} /* SS12_007 */,
   {"Goku",{"I won't let you destroy Earth!","\302\241No dejar\303\251 que destruyas la Tierra!"}} /* SS12_008 */,
   {"Goku",{"Kaioken... times four!","\302\241Kaioken... por cuatro!"}} /* SS12_009 */},
   ST_TRIG_RIVAL_PCT,0,8000,0x0u,0x0u,0x0u,0,0,0,0,0x0u,0x0u,0,0,2,2,{{"Goku",{"Enough, Vegeta. You can still leave.","Ya basta, Vegeta. Todav\303\255a puedes irte."}} /* SSA_012 */,{"Vegeta",{"Don't think I'll forget this humiliation.","No creas que voy a olvidar esta humillaci\303\263n."}} /* SSA_013 */},1,4,5,5,4,0}, /* Juan: Goku's Kaioken ULTIMATE (x4 Kamehameha, slot 4) vs Galick Gun */
  {1,ST_EVENT_SCENE,5,-1,
   {{"Goku",{"My body can't take that power again.","Mi cuerpo no soportar\303\241 otra vez ese poder."}} /* SS12_010 */,
   {"Vegeta",{"That still won't be enough to defeat me, Kakarot.","Eso a\303\272n no bastar\303\241 para derrotarme, Kakarotto."}} /* SS12_011 */,
   {"Vegeta",{"The moon is gone. I'll make another.","La luna ya no est\303\241. Har\303\251 otra."}} /* SS13_001 */,
   {"Goku",{"What are you doing?","\302\277Qu\303\251 est\303\241s haciendo?"}} /* SS13_002 */,
   {"Vegeta",{"Now you'll see a Saiyan's true power.","Ahora conocer\303\241s el verdadero poder Saiyajin."}} /* SS13_003 */},
   ST_TRIG_AFTER,1,0,0x0u,0x0u,0x0u,0,0,4,30,0x0u,0x0u,0,0,0,0,{{0,{0,0}}},0,0,0,0,0,0,1},
  {3,ST_EVENT_SCENE,2,-1,
   {{"Goku",{"So that's what my tail was for...","As\303\255 que para eso serv\303\255a mi cola..."}} /* SS13_004 */,
   {"Vegeta",{"Now you understand what it means to be a Saiyan.","Ahora entiendes lo que significa ser un Saiyajin."}} /* SS13_005 */},
   ST_TRIG_AFTER,1,0,0x0u,0x0u,0x0u,0,0,0,0,0x0u,0x0u,0,0},
  {75,ST_EVENT_SCENE,3,-1,
   {{"Goku",{"I need a few seconds... don't let him get close!","Necesito unos segundos... \302\241no dejes que se acerque!"}} /* SS13_006 */,
   {"Goku",{"Earth... lend me your energy.","Tierra... pr\303\251stame tu energ\303\255a."}} /* SS13_007 */,
   {"Vegeta",{"I won't give you time!","\302\241No te dar\303\251 tiempo!"}} /* SS13_008 */},
   ST_TRIG_RIVAL_PCT,0,0,0x1u,0x0u,0x0u,0,0,0,0,0x6u,0x0u,2,0,2,1,{{"Krillin",{"Goku, hold on! He can't keep fighting!","\302\241Goku, aguanta! \302\241Ya no puede seguir!"}} /* SSA_014 */},0,0,0,0,0,1},
  {1,ST_EVENT_SCENE,3,-1,
   {{"Gohan",{"Let go of my dad!","\302\241Suelta a mi pap\303\241!"}} /* SS14_001 */,
   {"Krillin",{"Gohan, stay clear of his tail!","Gohan, \302\241al\303\251jate de su cola!"}} /* SS14_002 */,
   {"Yajirobe",{"I came to watch... not fight a giant ape!","Yo ven\303\255a a mirar... \302\241no a pelear con un mono gigante!"}} /* SS14_003 */},
   ST_TRIG_AFTER,1,0,0x0u,0x0u,0x8u,0,0,0,0,0x0u,0x0u,0,0},
  {55,ST_EVENT_SCENE,3,-1,
   {{"Krillin",{"Yajirobe, get his tail! Now!","\302\241Yajirobe, ve por su cola! \302\241Ahora!"}} /* SS14_004 */,
   {"Krillin",{"Watch out! He's turning around!","\302\241Cuidado! \302\241Se est\303\241 dando la vuelta!"}} /* SS14_005 */,
   {"Yajirobe",{"When did I agree to this? Fine... I'm going!","\302\277Cu\303\241ndo acept\303\251 esto? Est\303\241 bien... \302\241Ya voy!"}} /* SS14_006 */},
   ST_TRIG_RIVAL_PCT,0,0,0x0u,0x0u,0x8u,0,0,4,29,0x0u,0x0u,0,0,2,1,{{"Krillin",{"Goku, hold on! He can't keep fighting!","\302\241Goku, aguanta! \302\241Ya no puede seguir!"}} /* SSA_014 */},4,-1,5,0,0,0,0,ST_CHOREO_TAILCUT},
  {1,ST_EVENT_SCENE,3,-1, /* right after the cut */
   {{"Vegeta",{"My tail!","\302\241Mi cola!"}} /* SS14_007 */,
   {"Krillin",{"Yajirobe, you cut his tail!","\302\241Yajirobe, le cortaste la cola!"}} /* SS14_008 */,
   {"Yajirobe",{"Don't shout! Now he's after me!","\302\241No grites! \302\241Ahora viene por m\303\255!"}} /* SS14_009 */},
   ST_TRIG_AFTER,1,0,0x0u,0x0u,0x8u,0,0,0,0,0x0u,0x0u,0,0}, /* (it follows the cut at once: no alternative) */
  {1,ST_EVENT_SCENE,3,-1,
   {{"Gohan",{"Get to Dad! I'll keep Vegeta busy.","\302\241Ve por pap\303\241! Yo distraer\303\251 a Vegeta."}} /* SS15_001 */,
   {"Krillin",{"Goku, save your strength. I'll take the Spirit Bomb.","Goku, guarda tus fuerzas. Yo me encargo de la Genkidama."}} /* SS15_002 */,
   {"Goku",{"There's still energy in it. Krillin, throw it when you see an opening!","A\303\272n queda energ\303\255a. Krilin, \302\241l\303\241nzala cuando veas una abertura!"}} /* SS15_003 */},
   ST_TRIG_AFTER,1,0,0x0u,0x0u,0x0u,0,0,0,0,0x0u,0x0u,0,0,0,0,{{0,{0,0}}},3,-1,1,0,0,0,0,ST_CHOREO_TRANSFER}, /* Goku -> Krillin */
  {30,ST_EVENT_SCENE,3,-1,
   {{"Krillin",{"You want me to throw it?","\302\277Quieres que la lance yo?"}} /* SS15_004 */,
   {"Goku",{"Yes. Wait for an opening. Gohan can't hold him much longer.","S\303\255. Espera una abertura. Gohan no podr\303\241 contenerlo mucho m\303\241s."}} /* SS15_005 */,
   {"Krillin",{"All right. I won't let you down.","De acuerdo. No voy a fallarles."}} /* SS15_006 */},
   ST_TRIG_RIVAL_PCT,0,0,0x0u,0x0u,0x0u,0,0,0,0,0x0u,0x0u,0,0,2,1,{{"Gohan",{"This time, we made it in time.","Esta vez llegamos a tiempo."}} /* SSA_015 */},0,0,0,0,0,0,1},
  {20,ST_EVENT_SCENE,4,-1,
   {{"Krillin",{"Now!","\302\241Ahora!"}} /* SS15_007 */,
   {"Vegeta",{"That attack won't reach me!","\302\241Ese ataque no me alcanzar\303\241!"}} /* SS15_008 */,
   {"Gohan",{"It's coming this way!","\302\241Viene hacia aqu\303\255!"}} /* SS15_009 */,
   {"Goku",{"Gohan, send it back!","\302\241Gohan, devu\303\251lvela!"}} /* SS15_010 */},
   ST_TRIG_RIVAL_PCT,0,1000000,0x0u,0x0u,0x0u,0,0,0,0,0x0u,0x0u,0,0,2,1,{{"Gohan",{"This time, we made it in time.","Esta vez llegamos a tiempo."}} /* SSA_015 */},3,4,5,0,0,3,0,ST_CHOREO_SPIRITBOMB,0},
  {1,ST_EVENT_SCENE,6,1,
   {{"Krillin",{"It hit him!","\302\241Le dio!"}} /* SS15_011 */,
   {"Gohan",{"Did we get him...?","\302\277Lo derrotamos...?"}} /* SS15_012 */,
   {"Vegeta",{"Not... yet.","Todav\303\255a... no."}} /* SS15_013 */,
   {"Vegeta",{"His tail... Gohan, look out!","Su cola... \302\241Gohan, cuidado!"}} /* SS16_001 */,
   {"Goku",{"Gohan... look up.","Gohan... mira hacia arriba."}} /* SS16_002 */,
   {"Krillin",{"The artificial moon is still there!","\302\241La luna artificial sigue ah\303\255!"}} /* SS16_003 */},
   ST_TRIG_AFTER,1,0,0x0u,0x0u,0x0u,0,0,1,180,0x0u,0x0u,3,0},
  {8,ST_EVENT_SCENE,3,1,
   {{"Krillin",{"Gohan, it's me! Can you hear me?","\302\241Gohan, soy yo! \302\277Puedes o\303\255rme?"}} /* SS16_004 */,
   {"Goku",{"Listen to me! Vegeta is right in front of you!","\302\241Esc\303\272chame! \302\241Vegeta est\303\241 justo delante de ti!"}} /* SS16_005 */,
   {"Vegeta",{"A child won't defeat me!","\302\241Un ni\303\261o no va a derrotarme!"}} /* SS16_006 */},
   ST_TRIG_AFTER,1,0,0x0u,0x0u,0x0u,0,1,0,0,0x0u,0x0u,0,0},
  {10,ST_EVENT_SCENE,2,-1,
   {{"Krillin",{"Gohan... he's breathing. He's alive.","Gohan... respira. Sigue vivo."}} /* SS16_007 */,
   {"Vegeta",{"I... can't get up.","No... puedo levantarme."}} /* SS16_008 */},
   ST_TRIG_AFTER,0,1000000,0x0u,0x0u,0x0u,0,0,1,13,0x0u,0x0u,0,0,0,0,{{0,{0,0}}},5,-1,2,0,0,0},
  {2,ST_EVENT_SCENE,6,-1,
   {{"Krillin",{"After everything he did... we should finish this!","Despu\303\251s de todo lo que hizo... \302\241debemos acabar con esto!"}} /* SS17_001 */,
   {"Goku",{"Krillin... wait. Let him go.","Krilin... espera. D\303\251jalo ir."}} /* SS17_002 */,
   {"Krillin",{"Goku, think of Yamcha. Think of the others.","Goku, piensa en Yamcha. Piensa en los dem\303\241s."}} /* SS17_003 */,
   {"Goku",{"I haven't forgotten them. I'll face Vegeta again when we're stronger.","No los he olvidado. Volver\303\251 a enfrentarme a Vegeta cuando seamos m\303\241s fuertes."}} /* SS17_004 */,
   {"Krillin",{"You're asking a lot of us.","Nos est\303\241s pidiendo mucho."}} /* SS17_005 */,
   {"Goku",{"I know. This time, I'm asking for myself.","Lo s\303\251. Esta vez te lo pido por m\303\255."}} /* SS17_006 */},
   ST_TRIG_AFTER,1,0,0x0u,0x0u,0x0u,0,0,0,0,0x0u,0x0u,0,0},
  {1,ST_EVENT_SCENE,3,-1,
   {{"Krillin",{"All right. But be ready when he comes back.","Est\303\241 bien. Pero prep\303\241rate cuando vuelva."}} /* SS17_007 */,
   {"Vegeta",{"Don't expect gratitude. I'll remember this day.","No esperes que te lo agradezca. Recordar\303\251 este d\303\255a."}} /* SS17_008 */,
   {"Krillin",{"We will too.","Nosotros tambi\303\251n."}} /* SS17_009 */},
   ST_TRIG_AFTER,1,0,0x0u,0x0u,0x0u,1,0,0,0,0x0u,0x0u,0,0}}},
 {2,{{3,ST_EVENT_SCENE,2,-1,
   {{"Goku",{"So that's what my tail was for...","As\303\255 que para eso serv\303\255a mi cola..."}} /* SS13_004 */,
   {"Vegeta",{"Now you understand what it means to be a Saiyan.","Ahora entiendes lo que significa ser un Saiyajin."}} /* SS13_005 */},
   ST_TRIG_TIME,0,0,0x0u,0x0u,0x0u,0,0},
  {70,ST_EVENT_SCENE,2,-1,
   {{"Goku",{"I need a few seconds... don't let him get close!","Necesito unos segundos... \302\241no dejes que se acerque!"}} /* SS13_006 */,
   {"Goku",{"Earth... lend me your energy.","Tierra... pr\303\251stame tu energ\303\255a."}} /* SS13_007 */},
   ST_TRIG_RIVAL_PCT,0,0,0x1u,0x0u,0x0u,1,0}}},
 {2,{{75,ST_EVENT_SCENE,3,-1,
   {{"Krillin",{"Yajirobe, get his tail! Now!","\302\241Yajirobe, ve por su cola! \302\241Ahora!"}} /* SS14_004 */,
   {"Krillin",{"Watch out! He's turning around!","\302\241Cuidado! \302\241Se est\303\241 dando la vuelta!"}} /* SS14_005 */,
   {"Yajirobe",{"When did I agree to this? Fine... I'm going!","\302\277Cu\303\241ndo acept\303\251 esto? Est\303\241 bien... \302\241Ya voy!"}} /* SS14_006 */},
   ST_TRIG_RIVAL_PCT,0,0,0x0u,0x0u,0x8u,0,0},
  {50,ST_EVENT_SCENE,2,-1,
   {{"Vegeta",{"My tail!","\302\241Mi cola!"}} /* SS14_007 */,
   {"Krillin",{"Yajirobe, you cut his tail!","\302\241Yajirobe, le cortaste la cola!"}} /* SS14_008 */},
   ST_TRIG_RIVAL_PCT,0,0,0x0u,0x0u,0xcu,1,0}}},
 {2,{{75,ST_EVENT_SCENE,3,-1,
   {{"Krillin",{"You want me to throw it?","\302\277Quieres que la lance yo?"}} /* SS15_004 */,
   {"Goku",{"Yes. Wait for an opening. Gohan can't hold him much longer.","S\303\255. Espera una abertura. Gohan no podr\303\241 contenerlo mucho m\303\241s."}} /* SS15_005 */,
   {"Krillin",{"All right. I won't let you down.","De acuerdo. No voy a fallarles."}} /* SS15_006 */},
   ST_TRIG_RIVAL_PCT,0,0,0x0u,0x0u,0x6u,0,0},
  {50,ST_EVENT_SCENE,4,-1,
   {{"Krillin",{"Now!","\302\241Ahora!"}} /* SS15_007 */,
   {"Vegeta",{"That attack won't reach me!","\302\241Ese ataque no me alcanzar\303\241!"}} /* SS15_008 */,
   {"Gohan",{"It's coming this way!","\302\241Viene hacia aqu\303\255!"}} /* SS15_009 */,
   {"Goku",{"Gohan, send it back!","\302\241Gohan, devu\303\251lvela!"}} /* SS15_010 */},
   ST_TRIG_RIVAL_PCT,0,1000000,0x0u,0x0u,0x6u,1,0}}},
 {2,{{12,ST_EVENT_SCENE,2,2,
   {{"Krillin",{"Gohan, it's me! Can you hear me?","\302\241Gohan, soy yo! \302\277Puedes o\303\255rme?"}} /* SS16_004 */,
   {"Goku",{"Listen to me! Vegeta is right in front of you!","\302\241Esc\303\272chame! \302\241Vegeta est\303\241 justo delante de ti!"}} /* SS16_005 */},
   ST_TRIG_TIME,0,0,0x0u,0x0u,0x8u,0,1},
  {45,ST_EVENT_SCENE,1,-1,
   {{"Vegeta",{"A child won't defeat me!","\302\241Un ni\303\261o no va a derrotarme!"}} /* SS16_006 */},
   ST_TRIG_RIVAL_PCT,0,1000000,0x0u,0x4u,0x0u,1,0}}},
 {1,{{0,ST_EVENT_SCENE,2,-1,
   {{"Vegeta",{"Don't expect gratitude. I'll remember this day.","No esperes que te lo agradezca. Recordar\303\251 este d\303\255a."}} /* SS17_008 */,
   {"Krillin",{"We will too.","Nosotros tambi\303\251n."}} /* SS17_009 */},
   ST_TRIG_TIME,1,0,0x0u,0x0u,0x0u,1,0}}},
 {0},{0}

,
{0},
{0},
 {0}
};
/* Outcome policy per chapter. LEGACY (chapters 1-2): the rival winning is the canonical story beat, the player winning
   the alternative. EVENT: the canonical clear is a player win after a canonical event (ST_CanonMask); a player win
   before it is the alternative (script section 18); losing is a genuine failure. WIN: any player win is the canonical
   clear ("an ordinary victory is normal success, not an alternate route"). */
enum { ST_POL_LEGACY, ST_POL_EVENT, ST_POL_WIN };
/* Phased chapters: events strictly in order (never superseded by extra damage), a KO of a phase enemy plays his event
   instead of ending the battle, the battle ends only through the canonical event or the player's defeat. */
static int ST_Phased(int chapter);
static const unsigned char stPolicy[ST_COUNT]={ST_POL_LEGACY,ST_POL_LEGACY,ST_POL_EVENT,ST_POL_EVENT,ST_POL_WIN,ST_POL_WIN,ST_POL_EVENT,ST_POL_EVENT,ST_POL_EVENT,ST_POL_EVENT,ST_POL_WIN,ST_POL_EVENT,ST_POL_EVENT,ST_POL_EVENT,ST_POL_EVENT,ST_POL_EVENT,ST_POL_EVENT,ST_POL_WIN,ST_POL_WIN,ST_POL_WIN,ST_POL_WIN
,
 ST_POL_WIN
};
/* The interlude played after a chapter is cleared, and the next one after it (-1: back to the story list, on
   ST_INTERLUDE_RETURN). Chapter 5 -> Kami's Lookout -> King Kai's planet -> chapter 6. */
static const signed char stInterludeNext[ST_COUNT]={-1,-1,-1,-1,17,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,18,-1,20,-1
,
 19
};
#define ST_INTERLUDE_RETURN 5
/* A chapter's stage from outside the disc (stages/<name with underscores>.unk), used when installed; the chapter's own
   disc stage otherwise. King Kai's planet is BT4's map (Juan, 2026-11-03). */
static const char *const stAddedStage[ST_COUNT]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,"King Kai Planet",0,"King Kai Planet"
,
 0
};
static int ST_Interlude(int ch){return ch>=0&&ch<ST_COUNT&&stChapters[ch].saga==ST_SAGA_INTERLUDE;}
/* No libc here: the header is also compiled into game files with their own declarations (src/battle/btl_seq.c). */
static int ST_StrEq(const char*a,const char*b){if(!a||!b)return 0;while(*a&&*a==*b){a++;b++;}return *a==*b;}
static int ST_StrLen(const char*s){int n=0;if(s)while(s[n])n++;return n;}
/* The cast index a line's speaker names, or -1 when nobody of the cast says it. */
static int ST_LineCast(int chapter,const StLine*l){int i;if(chapter<0||chapter>=ST_COUNT||!l)return -1;for(i=0;i<stCasts[chapter].count;i++)if(ST_StrEq(l->speaker,stCasts[chapter].actor[i].name))return i;return -1;}
/* Departure caption of a set (canonical: the chapter's own, alternative: "<rival> leaves."). */
static const char*ST_LeavesText(int chapter,int set,int lang){if(chapter<0||chapter>=ST_COUNT||lang<0||lang>1)return 0;return set==ST_SET_LEAVES?stCasts[chapter].leavesCanon[lang]:stLeaves[lang];}
/* 1 when the rival speaks the line (side 1), else 0. */
static int ST_LineSide(int chapter,const StLine*l){return chapter>=0&&chapter<ST_COUNT&&l&&ST_StrEq(l->speaker,stScenes[chapter].rivalSpeaker);}
/* A line of a scene set, or 0 (the leaves caption has no line). */
static const StLine*ST_SceneLine(int chapter,int set,int line){
 static const StLine survived={"Krillin",{"We held out until you got here. Nappa never let up.","Resistimos hasta que llegaste. Nappa no nos dio tregua."}};
 if(set==ST_SET_YAMCHA_ALIVE)return line==0?&survived:0;

 const StChapter*c;if(chapter<0||chapter>=ST_COUNT||line<0)return 0;c=&stChapters[chapter];
 switch(set){case ST_SET_INTRO:return line<c->introCount?&c->intro[line]:0;case ST_SET_CANON:return line<c->canonCount?&c->canon[line]:0;
 case ST_SET_ALT:return line<c->altCount?&c->alt[line]:0;case ST_SET_COMBAT:return line<stScenes[chapter].cueCount?&stScenes[chapter].cue[line].line:0;
 case ST_SET_BRIDGE:return line<stCasts[chapter].bridgeCount?&stCasts[chapter].bridge[line]:0;
 default:if(set>=ST_SET_ALTEV0&&set<ST_SET_ALTEV0+ST_EVENTS){int e=set-ST_SET_ALTEV0;return e<stEvents[chapter].count&&line<stEvents[chapter].event[e].altCount?&stEvents[chapter].event[e].alt[line]:0;}
  if(set>=ST_SET_EVENT0&&set<ST_SET_EVENT0+ST_EVENTS){int e=set-ST_SET_EVENT0;return e<stEvents[chapter].count&&line<stEvents[chapter].event[e].lineCount?&stEvents[chapter].event[e].line[line]:0;}}return 0;
}
/* How long a text-only line stays up: a reading time from its length, 2.5 to 7 seconds. */
static float ST_LineSeconds(const char*text){float s=1.6f+(float)ST_StrLen(text)*.055f;return s<2.5f?2.5f:s>7.f?7.f:s;}
static int ST_CueDue(const StCue*c,int seconds,int rivalPercent,int playerPercent){
 if(!c)return 0;switch(c->kind){case ST_CUE_TIME:return seconds>=c->value;case ST_CUE_RIVAL_HP:return rivalPercent<=c->value;case ST_CUE_PLAYER_HP:return playerPercent<=c->value;}return 0;
}
static void ST_Add(MsView*v,int id,int costume,int team){MsPick*p=&v->pick[v->total++];MsPick empty={0};*p=empty;p->chara=id;p->costume=costume;p->team=team;p->pad=-1;}
/* Fixed cast from stCasts: team 0 first (the first of them is the human, pad 0), then team 1, scene actors
   included (they are parked as soon as the fight exists). Returns 2 (fixed, review only) or 0. */
static int ST_BuildDraftEx(MsView*v,int chapter,int dense){
 const StCast*c;int team,i;if(!v||chapter<0||chapter>=ST_COUNT)return 0;c=&stCasts[chapter];
 v->team_count=2;v->mode=1;v->phase=1;v->total=0;v->slot=0;v->error=0;
 for(team=0;team<2;team++){if(team==1)v->a=v->total;for(i=0;i<c->count;i++)if(c->actor[i].team==team&&(!(c->actor[i].flags&ST_ACTOR_DENSE)||dense))ST_Add(v,c->actor[i].chara,c->actor[i].costume,team);}
 v->pick[0].pad=0;v->slot=-1;return 2;
}
/* The reduced-density draft (no ST_ACTOR_DENSE actors). */
static int ST_BuildDraft(MsView*v,int chapter){return ST_BuildDraftEx(v,chapter,0);}
/* Chapter 1: Piccolo winning is the alternative; Raditz winning by KO is canonical.
   Timeouts and abandoned fights fail. Explicit restarts do not pay rewards. */
static int ST_Outcome(int winner,int reason){
 if(reason&0x18000)return ST_NONE;
 if(!(reason&1))return ST_FAIL;
 if(winner==0)return ST_ALT;
 return winner==1?ST_CANON:ST_FAIL;
}
/* Chapters whose canonical ending is a PLAYER win (chapter 3: after the Special Beam Cannon). */
static int ST_Phased(int chapter){return chapter==6||chapter==11;}
/* The next chapter of the same (visible) saga after `chapter`, or -1. */
static int ST_NextVisible(int chapter){int i,best=-1;if(chapter<0||chapter>=ST_COUNT)return -1;for(i=0;i<ST_COUNT;i++)if(stChapters[i].saga==stChapters[chapter].saga&&stChapters[i].sequence>stChapters[chapter].sequence&&(best<0||stChapters[i].sequence<stChapters[best].sequence))best=i;return best;}
static int ST_CanonIsWin(int chapter){return chapter>=0&&chapter<ST_COUNT&&stPolicy[chapter]!=ST_POL_LEGACY;}
/* The events whose completion is the chapter's canonical clear (bit k = event k). */
static unsigned ST_CanonMask(int chapter){unsigned m=0;int k;if(chapter<0||chapter>=ST_COUNT)return 0;for(k=0;k<stEvents[chapter].count;k++)if(stEvents[chapter].event[k].canon)m|=1u<<k;return m;}
/* Per-chapter outcome. `events` = the authored R3 events completed this fight (bit k = event k). Chapter 3: a
   player win after the beam is canonical, a player win before it is the alternative (Goku survives), losing is a
   genuine failure. Chapters 1-2 keep ST_Outcome (Raditz winning is the canonical story beat). */
static int ST_OutcomeFor(int chapter,int winner,int reason,unsigned events){
 if(!ST_CanonIsWin(chapter))return ST_Outcome(winner,reason);
 if(reason&0x18000)return ST_NONE;
 if(!(reason&1)||winner!=0)return ST_FAIL;
 if(stPolicy[chapter]==ST_POL_WIN||stChapters[chapter].altCount<=0)return ST_CANON;
 return (events&ST_CanonMask(chapter))?ST_CANON:ST_ALT;
}
/* Progress bits that prove a real player win (ranks / trophies): the alternative bit, or any clear in a chapter whose
   canonical ending is itself a win. */
static unsigned ST_WinBits(int chapter){return ST_CanonIsWin(chapter)?(ST_DONE_CANON|ST_DONE_ALT):ST_DONE_ALT;}
/* Zeni for an outcome given saved progress bits; *progress gets the new bits. First
   rewards are paid once per save; repeats pay the smaller amount. */
static int ST_Reward(int chapter,int outcome,unsigned *progress){
 const StChapter*c;unsigned old;if(chapter<0||chapter>=ST_COUNT||!progress)return 0;c=&stChapters[chapter];old=*progress;
 if(outcome==ST_CANON){*progress|=ST_DONE_CANON;return old&ST_DONE_CANON?c->zeniCanonRepeat:c->zeniCanonFirst;}
 if(outcome==ST_ALT){*progress|=ST_DONE_CANON|ST_DONE_ALT;return old&ST_DONE_ALT?c->zeniAltRepeat:c->zeniAltFirst;}
 return 0;
}
/* Difficulty (approved by Juan 2026-11-01): the OPPONENT's max HP (percent) and attack levels per chapter and
   difficulty (Easy, Normal, Hard). Applied fresh from the unmodified values at every fight start, story only;
   defence untouched. Chapter 3's R3 moments are absolute HP values and do not scale. */
typedef struct StDifficulty {int hpPercent[3],attack[3];} StDifficulty;
static const StDifficulty stDifficulty[ST_COUNT]={
 {{85,100,130},{-2,0,3}},
 {{85,100,130},{-2,0,3}},
 {{90,100,140},{-1,0,4}},
 {{90,100,140},{-1,0,4}},
 {{90,100,140},{-1,0,4}},
 {{90,100,140},{-1,0,4}},
 {{85,100,120},{-1,0,1}},
 {{90,100,140},{-1,0,4}},
 {{90,100,140},{-1,0,4}},
 {{90,100,140},{-1,0,4}},
 {{90,100,140},{-1,0,4}},
 {{85,100,120},{-1,0,1}},
 {{90,100,140},{-1,0,4}},
 {{90,100,140},{-1,0,4}},
 {{90,100,140},{-1,0,4}},
 {{90,100,140},{-1,0,4}},
 {{90,100,140},{-1,0,4}},
 {{100,100,100},{0,0,0}},
 {{100,100,100},{0,0,0}}

,
{0},
{0},
 {{85,100,120},{-1,0,1}}
};
/* Story tune (Juan, 2026-11-02): per chapter and cast entry, on top of the costume, for every difficulty: max HP
   percent, attack levels (attack + technique slots) and defence levels. Canon power gaps of the Saiyan saga were
   large: Raditz far above Piccolo / Goku, Krillin below Goku. Proposed values for Juan's live check. */
typedef struct StTune {int hpPercent,attack,defence;} StTune;
static const StTune stTunes[ST_COUNT][ST_ACTORS]={
 {{100,0,0},{150,8,6}},                                   /* Piccolo, Raditz */
 {{100,0,0},{25,-20,-20},{150,8,6},{100,0,0},{100,0,0}},  /* Goku, Krillin (10,000 = the HP floor; attack/defence at the engine minimum, Juan 2026-11-02), Raditz, Gohan, Piccolo */
 {{100,0,0},{100,0,0},{150,8,6},{100,0,0}},               /* Goku, Piccolo, Raditz, Gohan */
 {{100,0,0},{130,6,6},{100,0,0}} /* saiyan_04_gohans_power */,
 {{100,0,0},{100,0,0},{100,0,0}} /* saiyan_05_one_year */,
 {{100,0,0},{100,0,0},{100,0,0},{100,0,0},{100,0,0},{60,-2,0},{60,-2,0},{60,-2,0},{60,-2,0},{60,-2,0}} /* saiyan_06_lookout */,
 /* chapter 7: Saibaman 1,200 vs post-training defenders; Nappa 4,000 dominates the group (no review bonuses stacked),
    Goku (8,000) clearly above him. Proposals for live tuning. */
 {{100,0,0},{100,0,0},{100,0,0},{100,0,0},{100,0,0},{100,0,0},{100,0,0},{60,0,0},{350,0,6},{100,0,0},{60,0,0},{60,0,0},{60,0,0}} /* saiyan_07_earths_last_defense */,
 {{100,0,0},{100,0,0},{100,0,0},{100,0,0},{70,2,0},{100,0,0},{100,0,0}} /* saiyan_08_six_seeds */,
 {{100,0,0},{100,0,0},{100,0,0},{100,0,0},{100,0,0},{170,10,8},{100,0,0}} /* saiyan_09_nappa */,
 {{100,0,0},{100,0,0},{100,0,0},{170,10,8},{100,0,0}} /* saiyan_10_three_hours */,
 {{100,0,0},{100,0,0},{100,0,0},{150,6,6},{100,0,0}} /* saiyan_11_goku_returns */,
 /* chapter 8: Vegeta 18,000 dominates base Goku (8,000) and Kaioken x2; x3 turns it; no review bonuses stacked */
 {{100,0,0},{100,0,0},{100,0,0},{100,0,0},{160,6,4},{100,0,0}} /* saiyan_12_battle_for_earth */,
 {{100,0,0},{150,10,10}} /* saiyan_13_artificial_moon */,
 {{100,0,0},{100,0,0},{100,0,0},{100,0,0},{130,8,8}} /* saiyan_14_yajirobe */,
 {{100,0,0},{100,0,0},{100,0,0},{120,8,6}} /* saiyan_15_last_energy */,
 {{100,0,0},{110,6,4},{90,4,4},{100,0,0},{100,0,0}} /* saiyan_16_last_giant */,
 {{100,0,0},{30,-10,-10},{100,0,0},{100,0,0}} /* saiyan_17_let_him_live */,
 {{100,0,0},{100,0,0},{100,0,0},{100,0,0},{100,0,0}} /* saiyan_05b_lookout_training */,
 {{100,0,0},{100,0,0}} /* saiyan_05c_king_kai */

,
{0},
{0},
 {{100,0,0},{100,0,0},{100,0,0},{100,0,0},{100,0,0},{140,0,2},{140,0,2}}
};
/* Max HP after the difficulty: rounded to 1,000, at least 10,000. */
static int ST_ScaleHealth(int chapter,int difficulty,int base){
 long long v;if(chapter<0||chapter>=ST_COUNT||difficulty<0||difficulty>2||base<=0)return base;
 v=(long long)base*stDifficulty[chapter].hpPercent[difficulty]/100;v=(v+500)/1000*1000;if(v<10000)v=10000;if(v>2000000000)v=2000000000;return (int)v;
}
/* An attack level after the difficulty, kept in the engine's -20..40 range. */
static int ST_ScaleAttack(int chapter,int difficulty,int level){
 int v;if(chapter<0||chapter>=ST_COUNT||difficulty<0||difficulty>2)return level;v=level+stDifficulty[chapter].attack[difficulty];return v<-20?-20:v>40?40:v;
}
/* Raditz stays in his base form in chapter 1 (no Great Ape), and nobody fuses. */
static int ST_FormAllowed(int chapter,int chara,int target){(void)chapter;return target==chara;}
/* The story form an event authorizes for a cast character: the target of the chapter's first event that transforms a
   cast entry of `source`, else -1 (its slot is added to that fighter's form table for story fights only). */
static int ST_StoryFormFor(int chapter,int source){int k,form[ST_ACTORS],i;const StEvents*e;if(chapter<0||chapter>=ST_COUNT)return -1;e=&stEvents[chapter];
 for(i=0;i<stCasts[chapter].count&&i<ST_ACTORS;i++)form[i]=stCasts[chapter].actor[i].chara; /* forms change in event order */
 for(k=0;k<e->count;k++){int w=e->event[k].transformCast;if(e->event[k].transformTo<=0||w<0||w>=stCasts[chapter].count)continue;
  if(form[w]==source)return e->event[k].transformTo;form[w]=e->event[k].transformTo;}return -1;}

#ifdef __cplusplus
extern "C" {
#endif
void Port_StoryChoose(int),Port_StoryClear(void),Port_StoryAcknowledge(void),Port_StoryFightStarted(void);
int Port_StoryActive(void),Port_StoryChapter(void),Port_StoryStage(void),Port_StoryBgm(void),Port_StoryDraft(MsView*);
int Port_StoryLastOutcome(int *chapter,int *zeni);
unsigned Port_StoryProgress(int chapter);
int Port_StoryClearRank(int chapter);
int Port_StorySagaState(int saga); /* authored1, progression-open2, complete4 */
/* Result receipts: ball0..6, newly unlocked chapter index, or -1 for none.
   Already committed to the save; renderers must only present these values. */
int Port_StoryGrantedBall(void),Port_StoryUnlockedChapter(void),Port_StoryUnlockedSaga(void);
/* Pure campaign evaluator: minimum rank across EVERY required chapter.
   Hard includes Normal/Easy. Canonical and alternative endings both clear. */
static unsigned ST_CampaignTrophies(const int *ranks,int count,int finalized){
 int i,min=3;if(!finalized||!ranks||count<1||count>ST_RANK_CAPACITY)return 0;
 for(i=0;i<count;i++){if(ranks[i]<1||ranks[i]>3)return 0;if(ranks[i]<min)min=ranks[i];}
 return (1u<<20)|(min>=2?(1u<<21):0)|(min>=3?(1u<<22):0);
}
/* Saiyan saga trophies 23..25: the lowest best-WIN rank across every required saga-0 combat chapter (0 = never won). */
static unsigned ST_SaiyanTrophies(const int *ranks,int count){
 int i,min=3;if(!ranks||count<1||count>ST_RANK_CAPACITY)return 0;
 for(i=0;i<count;i++){if(ranks[i]<1||ranks[i]>3)return 0;if(ranks[i]<min)min=ranks[i];}
 return (1u<<23)|(min>=2?(1u<<24):0)|(min>=3?(1u<<25):0);
}
unsigned Port_StoryResult(int winner,int reason,int eligible);
int Port_StoryTakeReturn(void),Port_StoryCpuLevel(void),Port_StoryBalanceBefore(void),Port_StoryBalanceAfter(void);
void Port_StorySubtitle(int chapter,int set,int line);
int Port_StoryCastActor(int cast);          /* battle actor of a cast entry, or -1 */
int Port_StorySceneActor(int actor);         /* 1 for a scene-only actor of the running chapter */
void Port_StorySceneShow(int actor,int on);  /* the cutscene takes / gives back a scene-only actor */
int Port_StorySceneParked(int actor);        /* scene-only and not in a cutscene: kept out of the fight */
int Port_StoryDifficulty(void);
int Port_StoryLegacyClear(int chapter);
int Port_StoryHasCheckpoint(void);void Port_StoryRestartCheckpoint(void);void Port_StoryRequestCheckpoint(void);
/* The story tune of a battle actor (chapter tune, then the difficulty for the opponent): 1 when it applies. */
int Port_StoryTune(int actor,int *hpPercent,int *attack,int *defence);
int Port_StorySubtitleView(int *chapter,int *set,int *line);
void Port_StoryPrompt(int visible);
int Port_StoryPromptView(void);
void Port_StoryPresentation(int chapter,int phase,int progress);
int Port_StoryPresentationView(int *chapter,int *phase,int *progress);
#ifdef __cplusplus
}
#endif
/* Display names only: keep speaker/cast keys stable for cinematic actor resolution. */
static const char*ST_SpeakerName(const char*name,int lang){return lang==1&&ST_StrEq(name,"Piccolo")?"Piccoro":name;}


#endif
