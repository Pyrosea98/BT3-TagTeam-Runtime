
#ifndef MOD_SCENARIOS_H
#define MOD_SCENARIOS_H
#include "mod_select.h"
#define SC_COUNT 18
#define SC_FAMILIES 6
typedef struct ScRoute {int family,rank,player,achievement;const char *name[2],*brief[2];} ScRoute;
static const ScRoute scRoutes[SC_COUNT]={
 {0,0,-1,2,{"Protect the androids","Protege a los androides"},{"Cell hunts Androids 17 and 18 while you seek an opening. Three sides share one battlefield. Defeat Cell, even if he manages to become perfect.","Cell acecha a los androides 17 y 18 mientras buscas una oportunidad. Tres bandos comparten el campo de batalla. Derrota a Cell, aunque alcance la perfecci\303\263n."}},
 {0,1,100,3,{"Android 16","Androide 16"},{"Cell has unleashed a terrifying power. Android 16 stands with 17 and 18 against him. Defeat this fearsome enemy, even if he manages to absorb your friends.","Cell ha desatado un poder aterrador. El androide 16 une fuerzas con 17 y 18 para enfrentarlo. Vence a este enemigo, aunque consiga absorber a tus amigos."}},
 {0,2,23,4,{"Piccolo (End)","Piccoro (Fin)"},{"Piccolo stands between Cell and his perfect form. Use his strength and cunning to win, even if Cell fulfills his ambition.","Piccoro se interpone entre Cell y su forma perfecta. Usa su fuerza y su astucia para vencer, aunque Cell cumpla su ambici\303\263n."}},
 {0,3,33,5,{"A perfect opponent","Un rival perfecto"},{"Vegeta wants a worthy opponent. Let Cell absorb 17 and 18, then prove that even perfection is no match for Super Vegeta.","Vegeta quiere un rival a su altura. Deja que Cell absorba a 17 y 18 y demuestra que ni la perfecci\303\263n puede con Super Vegeta."}},
 {1,1,-1,6,{"Android Assault","Asalto androide"},{"Three androids, one mission: destroy you. Choose your fighter and defeat Android 13, even if he claims the power of his companions.","Tres androides, una misi\303\263n: destruirte. Elige a tu luchador y derrota al androide 13, incluso si re\303\272ne el poder de sus compa\303\261eros."}},
 {1,2,2,7,{"Three Super Saiyans","Tres Super Saiyans"},{"Goku, Vegeta and Trunks unite as Super Saiyans. Break through the android assault and defeat 13, whatever form he takes.","Goku, Vegeta y Trunks unen sus fuerzas como Super Saiyans. Abre paso entre los androides y derrota a 13, tome la forma que tome."}},
 {1,3,2,8,{"Alone against Super 13","Solo contra Super 13"},{"This time Goku stands alone. Defeat 14 and 15, let their remains empower 13, then overcome Super Android 13 with your Super Saiyan strength.","Esta vez Goku est\303\241 solo. Derrota a 14 y 15, deja que sus restos fortalezcan a 13 y supera a Super Androide 13 con tu fuerza de Super Saiyan."}},
 {2,1,-1,9,{"Stop Buu's Feast","Det\303\251n el fest\303\255n de Buu"},{"Buu has his eyes on Piccolo and Gotenks. Join them and stop his feast; even if he devours an ally, the battle is not lost.","Buu les ha echado el ojo a Piccoro y Gotenks. \303\232nete a ellos y det\303\251n su fest\303\255n; aunque devore a un aliado, la batalla no est\303\241 perdida."}},
 {2,2,-1,10,{"Nobody on the menu","Nadie en el menu"},{"Not a single bite! Protect Piccolo and Gotenks and defeat Buu before he absorbs anyone. Even a fallen friend is not safe from his hunger.","\302\241Ni un solo bocado! Protege a Piccoro y Gotenks y vence a Buu sin que absorba a nadie. Ni un compa\303\261ero ca\303\255do est\303\241 a salvo de su hambre."}},
 {2,3,21,11,{"Too confident","Demasiada confianza"},{"Gohan trusts his ultimate power a little too much. Let Buu absorb an ally, then defeat him. Be careful: if Buutenks wears you down, you could be next.","Gohan conf\303\255a demasiado en su poder definitivo. Deja que Buu absorba a un aliado y luego derr\303\263talo. Cuidado: si Buutenks te deja sin fuerzas, podr\303\255as ser el siguiente."}},
 {3,1,-1,12,{"Baby's Invasion","La invasi\303\263n de Baby"},{"Goku and Vegeta are exhausted, with no strength left to resist. Once the two Babys take their bodies, stand alone against them and their possessed companions.","Goku y Vegeta est\303\241n debilitados y sin fuerzas para resistir. Cuando los dos Babys se apoderen de sus cuerpos, enfr\303\251ntate en solitario a ellos y a sus compa\303\261eros pose\303\255dos."}},
 {3,2,7,13,{"Goku against Baby","Goku contra Baby"},{"Vegeta is running out of strength, and Baby is waiting for his chance. Once he takes Vegeta's body, unleash Goku's power and free Earth from the invaders.","A Vegeta le quedan pocas fuerzas y Baby espera su oportunidad. Cuando se adue\303\261e de su cuerpo, desata el poder de Goku y libera la Tierra de los invasores."}},
 {3,3,186,14,{"Pan and Uub","Pan y Uub"},{"Grandpa can barely stand, and Baby is closing in. When he takes Goku's body, Pan and Uub must face him and his possessed allies. Bring everyone home!","El abuelo apenas puede mantenerse en pie y Baby se acerca. Cuando tome el cuerpo de Goku, Pan y Uub deber\303\241n enfrentarlo junto a sus aliados pose\303\255dos. \302\241Tr\303\241iganlos de vuelta!"}},
 {4,2,-1,0,{"Seven Shadow Dragons","Los siete dragones"},{"Seven corrupted Dragon Balls, seven fearsome enemies. Face the Shadow Dragons alone and defeat them all. Beware: Omega can claim the power of his fallen brothers.","Siete esferas corrompidas, siete enemigos temibles. Enfrenta solo a los dragones oscuros y derr\303\263talos a todos. Cuidado: Omega puede reclamar el poder de sus hermanos ca\303\255dos."}},
 {4,3,10,1,{"Legend of Super Saiyan 4","Leyenda del Super Saiyan 4"},{"One last battle for the fate of the Dragon Balls. Defeat all seven Shadow Dragons and claim victory as Super Saiyan 4 Goku.","Una \303\272ltima batalla por el destino de las esferas del drag\303\263n. Derrota a los siete dragones oscuros y alcanza la victoria como Goku Super Saiyan 4."}},
 {5,1,-1,15,{"Multiverse Buu","Buu Multiverso"},{"Goku and Vegeta are weakened and drained. Stand beside them against this strange Buu, whose hunger seeks Vegeta first, then Goku. Find a way to win.","Goku y Vegeta est\303\241n debilitados y sin fuerzas. Lucha a su lado contra este extra\303\261o Buu, cuyo apetito busca primero a Vegeta y despu\303\251s a Goku. Encuentra la forma de vencer."}},
 {5,2,7,16,{"The GT answer","La respuesta GT"},{"Buu seeks the strength of the exhausted Goku and Vegeta. Let him absorb them and reach Full Power, then unite the GT warriors as Vegetto. Win without becoming a Super Saiyan.","Buu ans\303\255a la fuerza de Goku y Vegeta, ya agotados. Deja que los absorba y alcance su m\303\241ximo poder; despu\303\251s une a los guerreros GT en Vegetto. Vence sin recurrir al Super Saiyan."}},
 {5,3,21,17,{"Gohan's last stand","La \303\272ltima batalla de Gohan"},{"Goku and Vegeta have little strength left, but Gohan, Gotenks and Piccolo refuse to surrender. Lead them against Multiverse Buu and end his hunger for Saiyan power.","A Goku y Vegeta les quedan pocas fuerzas, pero Gohan, Gotenks y Piccoro no piensan rendirse. Gu\303\255alos contra Buu Multiverso y acaba con su hambre de poder Saiyan."}}
};
static const char *scFamilies[2][SC_FAMILIES]={{"Prevent Perfection","Android Assault","Stop Buu's Feast","Baby's Invasion","Seven Shadow Dragons","Multiverse Buu"},{"Impide la perfecci\303\263n","Asalto androide","Det\303\251n el fest\303\255n de Buu","La invasi\303\263n de Baby","Los siete dragones","Buu Multiverso"}};
static int SC_First(int family){int i;for(i=0;i<SC_COUNT;i++)if(scRoutes[i].family==family)return i;return 0;}
static int SC_DefaultStage(int route){static const int stages[SC_FAMILIES]={11,14,1,29,29,1};return route>=0&&route<SC_COUNT?stages[scRoutes[route].family]:-1;}
static int SC_Count(int family){int i,n=0;for(i=0;i<SC_COUNT;i++)if(scRoutes[i].family==family)n++;return n;}
static void SC_Add(MsView*v,int id,int costume,int team){MsPick*p=&v->pick[v->total++];MsPick empty={0};*p=empty;p->chara=id;p->costume=costume;p->team=team;p->pad=-1;}
static int SC_BuildDraft(MsView*v,int route,int variant,unsigned*weak){
 int i,slot,player;unsigned weakSlots=0;const ScRoute*r;if(route<0||route>=SC_COUNT||!v)return 0;r=&scRoutes[route];
 v->team_count=2;v->mode=1;v->phase=1;v->total=0;v->slot=0;v->error=0;player=r->player;
 if(route==16&&variant%2)player=122;
 if(route==14){static const int g[]={10,200,233};player=g[variant%3];}
 SC_Add(v,player,player==7?2:0,0);v->pick[0].pad=0;
 switch(r->family){
 case 0:if(route==1){SC_Add(v,101,0,0);SC_Add(v,102,1,0);}else v->team_count=3;break;
 case 1:if(route==5){SC_Add(v,33,0,0);SC_Add(v,43,0,0);}break;
 case 2:SC_Add(v,48,0,0);SC_Add(v,23,0,0);break;
 case 3:
  if(route==10){weakSlots|=1u<<v->total;SC_Add(v,122,0,0);weakSlots|=1u<<v->total;SC_Add(v,7,2,0);}
  if(route==11){weakSlots|=1u<<v->total;SC_Add(v,122,0,0);}
  if(route==12){SC_Add(v,64,0,0);weakSlots|=1u<<v->total;SC_Add(v,7,2,0);}break;
 case 5:
  if(route==16)SC_Add(v,player==7?122:7,player==7?0:2,0);
  if(route==17){SC_Add(v,48,0,0);SC_Add(v,23,0,0);}
  weakSlots|=1u<<v->total;SC_Add(v,3,0,0);weakSlots|=1u<<v->total;SC_Add(v,34,0,0);break;
 }
 v->a=v->total;
 switch(r->family){
 case 0:SC_Add(v,105,0,1);if(route!=1){SC_Add(v,101,0,2);SC_Add(v,102,1,2);}break;
 case 1:SC_Add(v,129,0,1);SC_Add(v,252,0,1);SC_Add(v,253,0,1);break;
 case 2:SC_Add(v,114,0,1);break;
 case 3:SC_Add(v,255,0,1);if(route==10)SC_Add(v,255,0,1);SC_Add(v,72,2,1);SC_Add(v,81,2,1);SC_Add(v,74,2,1);break;
 case 4:{static const int dragons[]={146,261,76,145,275,213,276};for(i=0;i<7;i++)SC_Add(v,dragons[i],0,1);}break;
 case 5:SC_Add(v,291,0,1);break;
 }
 if(weak)*weak=weakSlots;
 v->slot=player<0?0:-1;return player<0?1:2;
}

#ifdef __cplusplus
extern "C" {
#endif
void Port_ScenarioChoose(int,int),Port_ScenarioClear(void),Port_ScenarioRoundReset(void);
int Port_ScenarioActive(void),Port_ScenarioStage(void),Port_ScenarioBgm(void),Port_ScenarioDraft(MsView*),Port_ScenarioWeak(int);
int Port_ScenarioFormAllowed(int,int),Port_ScenarioFusionAllowed(int,int),Port_ScenarioAbsorbAllowed(int,int);
void Port_ScenarioConsumed(int,int),Port_ScenarioObserve(int,int),Port_ScenarioSetFinalHp(const int*,int);
int Port_ScenarioCpuScope(int);
int Port_ScenarioIndependentTeams(void),Port_ScenarioObjectiveEnd(const int*,int),Port_ScenarioBossBuff(int),Port_ScenarioHealthMax(int,int);
unsigned Port_ScenarioResult(int,int,int,const int*,int,int);
#ifdef __cplusplus
}
#endif
#endif
