#ifndef MOD_STORY_COMMENTARY_H
#define MOD_STORY_COMMENTARY_H
/* Original guide comments, keyed by stable chapter ID, not menu row. EN/ES.
   Future chapters fall back to their own recap and their saga guide's idle bank.
   Fill this table when authoring a chapter; do not reuse chapter1's greeting. */
typedef struct StGuideChapter {
 const char *id;
 const char *opening[2],*canon[2],*alternate[2],*failure[2];
 const char *idle[2][STG_IDLE_LINES];
} StGuideChapter;
static const StGuideChapter stGuideChapters[]={
 {"saiyan_01_raditz_wasteland",
  {"That power just appeared. It isn't Goku. The stranger is looking for someone... and I won't let him pass.","Esa energ\303\255a acaba de llegar. No es Goku. El desconocido busca a alguien... y no pienso dejarlo pasar."},
  {"Now you see why I followed him. An enemy like that cannot be left wandering around Earth.","Ahora entiendes por qu\303\251 fui tras \303\251l. No podemos dejar que un enemigo as\303\255 ande suelto por la Tierra."},
  {"He wasn't invincible. If he escaped, he'll return with someone stronger. Stay alert.","No era invencible. Pero si huy\303\263, volver\303\241 con alguien m\303\241s fuerte. No bajes la guardia."},
  {"Stop attacking blindly. Watch his movements and wait for an opening.","Deja de atacar a ciegas. Observa sus movimientos y espera una abertura."},
  {{"His scouter noticed another power. We should find out whose it is.","That armor is tough, but every fighter leaves an opening.","I would rather train alone. Raditz made that impossible."},
   {"Su rastreador detect\303\263 otra energ\303\255a. Habr\303\241 que averiguar de qui\303\251n es.","Esa armadura es resistente, pero todo luchador deja una abertura.","Preferir\303\255a entrenar solo. Raditz se ha encargado de impedirlo."}}},
 {"saiyan_02_kame_house",
  {"Raditz has found Goku, and the boy is caught in the middle. Keep your eyes on him; this visit will not end quietly.","Raditz ha encontrado a Goku y el ni\303\261o est\303\241 en medio de todo. No lo pierdas de vista; esta visita no terminar\303\241 en paz."},
  {"Goku went after his son even knowing Raditz was stronger. For once, I'll fight beside him.","Goku fue tras su hijo aun sabiendo que Raditz era m\303\241s fuerte. Por una vez, luchar\303\251 a su lado."},
  {"Gohan is safe. Raditz failed to take him. Now we need to learn why he came.","Gohan est\303\241 a salvo. Raditz no consigui\303\263 llev\303\241rselo; ahora tenemos que averiguar por qu\303\251 vino."},
  {"Keep your eyes on Gohan. Raditz will use any opening to take him.","No pierdas de vista a Gohan. Raditz usar\303\241 cualquier descuido para llev\303\241rselo."},
  {{"A brother from space. Goku certainly knows how to attract trouble.","Krillin is brave, but rushing Raditz head-on will not help the boy.","That child has more power than he realizes. Remember that."},
   {"Un hermano del espacio. Goku s\303\255 que sabe atraer problemas.","Krilin es valiente, pero lanzarse de frente contra Raditz no ayudar\303\241 al ni\303\261o.","Ese ni\303\261o tiene m\303\241s poder del que imagina. Recu\303\251rdalo."}}},
 {"saiyan_03_unlikely_alliance",
  {"Don't mistake this for friendship, Goku. We hold Raditz down and get Gohan out.","No confundas esto con una alianza entre amigos, Goku. Sujetaremos a Raditz y sacaremos a Gohan de ah\303\255."},
  {"Goku held him, knowing what it would cost. His sacrifice gave Gohan a chance.","Goku lo sujet\303\263 aunque sab\303\255a lo que le costar\303\255a. Su sacrificio le dio a Gohan una oportunidad."},
  {"Raditz is down and Goku is alive. Use the time you bought; the other Saiyans are coming.","Raditz cay\303\263 y Goku sigue vivo. Aprovecha el tiempo que ganaste: los otros Saiyajin vendr\303\241n."},
  {"Charging that technique leaves an opening. We need to cover each other, or Raditz will tear us apart.","Cargar esa t\303\251cnica me deja expuesto. Tenemos que cubrirnos, o Raditz acabar\303\241 con los dos."},
  {{"Gohan's power erupted when his father was in danger.","Goku must hold Raditz steady. I cannot afford to miss.","An alliance does not make us friends. We simply share an enemy."},
   {"El poder de Gohan despert\303\263 cuando su padre estuvo en peligro.","Goku debe sujetar bien a Raditz. No puedo permitirme fallar.","Esta alianza no nos hace amigos. Solo compartimos un enemigo."}}},
 /* chapters 4-8 (Codex, SAIYAN-SAGA-GUIDE-COMMENTS-2026-11-02.json; anchors 7 and 12 for the merged battles) */
 {"saiyan_04_gohans_power",
  {"Gohan isn't a fighter yet. But under that moon, a power he doesn't understand broke loose.","Gohan no es un guerrero. Pero bajo esa luna apareci\303\263 un poder que ni \303\251l entiende."},
  {"I destroyed the moon and Gohan returned to normal. He's exhausted. Let him rest.","Destru\303\255 la luna y Gohan volvi\303\263 a la normalidad. Ahora est\303\241 agotado; d\303\251jalo descansar."},
  {"You stopped him before he wrecked everything. That doesn't mean he can control his power yet.","Lo detuviste antes de que arrasara con todo. Eso no significa que ya sepa controlar su fuerza."},
  {"Don't fight him as if he means to hurt you. Find a way to stop him without hurting him.","No luches contra \303\251l como si quisiera hacerte da\303\261o. Busca c\303\263mo detenerlo sin lastimarlo."},
  {{"Goku used to turn into that monster too. I should have remembered sooner.","Don\342\200\231t mistake his fear for weakness. He needs to learn how to control his strength.","I won\342\200\231t let one moonlit night put an end to his training."},
   {"Goku tambi\303\251n se convert\303\255a en ese monstruo. Deb\303\255 haberlo recordado antes.","No confundas su miedo con falta de fuerza. Lo que le falta es aprender a dominarla.","No pienso dejar que una noche de luna termine con su entrenamiento."}}},
 {"saiyan_05_one_year",
  {"Time is running out. Gohan has to learn to defend himself before the Saiyans arrive.","El tiempo se acaba. Gohan debe aprender a defenderse antes de que los Saiyajin lleguen."},
  {"He stopped running. He still has a long way to go, but now he can stand his ground.","Ha dejado de huir. Todav\303\255a le falta mucho, pero ya puede mantenerse en pie."},
  {0,0},
  {"If he freezes when an enemy appears, he can't protect himself. We'll try again.","Si se paraliza al ver al enemigo, no podr\303\241 protegerse. Volveremos a intentarlo."},
  {{"Sometimes he asks about his father. I don\342\200\231t have an answer for everything.","Strength is useless if he freezes at the sight of an enemy. That takes training too.","Goku had better be making good use of this time. I\342\200\231m doing my part."},
   {"A veces pregunta por su padre. No tengo una respuesta para todo.","La fuerza no sirve si se paraliza al ver al rival. Eso tambi\303\251n se entrena.","Goku m\303\241s vale que est\303\251 aprovechando este tiempo. Yo estoy haciendo mi parte."}}},
 {"saiyan_06_lookout",
  {"The others are training too. When Vegeta and Nappa arrive, they'll have to cover one another.","Los dem\303\241s tambi\303\251n entrenan. Cuando lleguen Vegeta y Nappa, tendr\303\241n que cubrirse entre todos."},
  {"They've improved. I hope they remember this when Nappa is standing in front of them.","Mejoraron. Espero que recuerden esto cuando Nappa est\303\251 delante de ellos."},
  {0,0},
  {"Training together means nothing if everyone rushes in alone. Learn to support each other.","Entrenar juntos no sirve si cada uno se lanza por su cuenta. Aprendan a apoyarse."},
  {{"The Lookout looks peaceful. That doesn\342\200\231t mean the training is easy.","Knowing an opponent\342\200\231s moves isn\342\200\231t enough. You have to know when to use your own.","They\342\200\231d better not waste their strength trying to impress each other."},
   {"El templo parece tranquilo. Eso no significa que el entrenamiento sea f\303\241cil.","No basta con conocer los movimientos del rival. Hay que saber cu\303\241ndo usarlos.","Que no gasten sus fuerzas tratando de impresionar a los dem\303\241s."}}},
 {"saiyan_07_saiyans_arrive",
  {"They're here. Goku hasn't arrived yet. Hold formation and don't leave Gohan alone.","Ya llegaron. Goku a\303\272n no est\303\241 aqu\303\255. Mantengan la formaci\303\263n y no dejen solo a Gohan."},
  {"Gohan survived. I wasn\342\200\231t there to see it, but Goku arrived in time to protect him. Now it\342\200\231s up to them.","Gohan sobrevivi\303\263. No pude quedarme para verlo, pero Goku lleg\303\263 a tiempo de protegerlo. Ahora todo depende de ellos."},
  {"Nappa fell before events could follow their expected course. Vegeta won't leave because of it.","Nappa cay\303\263 antes de que todo siguiera el mismo curso. Vegeta no va a marcharse por eso."},
  {"Don't get separated. Nappa wants us to attack without thinking.","No te separes del grupo. Nappa quiere que ataquemos sin pensar."},
  {{"Saibaimen don\342\200\231t look like much until one gets too close.","Krillin and I can cover more ground together. A distracted enemy stops watching the others.","Gohan has the strength. He needs the courage to use it when it matters most."},
   {"Los Saibaiman parecen poca cosa hasta que uno se acerca demasiado.","Krilin y yo podemos cubrir m\303\241s terreno juntos. Un rival distra\303\255do deja de mirar a los dem\303\241s.","Gohan tiene fuerza. Necesita valor para usarla cuando m\303\241s importa."}}},
 {"saiyan_12_kaioken",
  {"Goku returned with a new technique, but Vegeta still has plenty of strength. Don't let pride decide the fight.","Goku volvi\303\263 con una t\303\251cnica nueva, pero Vegeta a\303\272n tiene fuerzas de sobra. No permitas que el orgullo decida la pelea."},
  {"Vegeta withdrew. Gohan, Krillin and Yajirobe bought Goku the time he needed. This isn't over yet.","Vegeta se retir\303\263. Gohan, Krilin y Yajirobe nos dieron el tiempo que Goku necesitaba. Esto a\303\272n no ha terminado."},
  {"This time Vegeta fell before we reached the brink. Don't mistake a win for the end of the Saiyan threat.","Esta vez Vegeta cay\303\263 antes de llegar al l\303\255mite. No confundas una victoria con el final de la amenaza Saiyajin."},
  {"Don't spend all your strength on Kaio-ken. If Goku falls, the others must finish the fight.","No gastes toda tu fuerza en Kaioken. Si Goku cae, los dem\303\241s tendr\303\241n que terminar la pelea."},
  {{"The Kaio-Ken can turn a fight around. It can also leave Goku too exhausted to finish it.","If Vegeta looks away, don\342\200\231t waste the opening. Sometimes that matters more than strength.","I never expected Yajirobe to get involved in something like this. I suppose everyone has their moment."},
   {"El Kaioken puede cambiar una pelea. Tambi\303\251n puede dejar a Goku sin fuerzas para terminarla.","Si Vegeta mira a otro lado, no desperdicies la oportunidad. A veces eso vale m\303\241s que la fuerza.","No esperaba que Yajirobe se metiera en algo as\303\255. Supongo que todos tienen su momento."}}}
};
static const StGuideChapter *STG_Chapter(const char *id){
 if(!id)return nullptr;
 for(unsigned i=0;i<sizeof(stGuideChapters)/sizeof(stGuideChapters[0]);i++)if(!strcmp(id,stGuideChapters[i].id))return &stGuideChapters[i];
 return nullptr;
}
static const char *STG_ChapterOpening(const StGuideChapter *c,int language,int outcome){
 if(!c)return nullptr;
 return outcome==ST_CANON?c->canon[language]:outcome==ST_ALT?c->alternate[language]:outcome==ST_FAIL?c->failure[language]:c->opening[language];
}
#endif
