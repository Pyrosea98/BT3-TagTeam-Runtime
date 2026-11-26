#ifndef MOD_STORY_WARDROBE_H
#define MOD_STORY_WARDROBE_H
/* Internal chapter and cast indices, not displayed chapter numbers. */
static int ST_ScriptedWardrobe(int ch,int cast){
 if(ch>=0&&ch<=2)return cast>=0;
 if(ch==6)return cast==0||cast==6||cast==8;
 if(ch==11)return cast==0||cast==4;
 return 0;
}
static int ST_GohanTailAbsent(int ch){return ch==4||ch==5||ch==6||ch==18||ch==19||ch==20;}
#endif
