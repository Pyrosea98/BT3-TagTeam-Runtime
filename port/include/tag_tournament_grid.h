#ifndef TAG_TOURNAMENT_GRID_H
#define TAG_TOURNAMENT_GRID_H
/* Same 36-byte cell as the game's grid. Retain playable unlocked entries;
   remove disabled/custom placeholders and pad only the last seven-cell row. */
struct TTGridCell {int id,count,form[7];};
static int TT_CompactGrid(struct TTGridCell *grid,int count,int capacity){
 int i,n=0,pad;if(!grid||count<0||count>capacity)return -1;
 for(i=0;i<count;i++)if(grid[i].id>=0&&(grid[i].id<=161||(grid[i].id>=165&&grid[i].id<=250))){if(n!=i)grid[n]=grid[i];n++;}
 if(!n)return 0;
 pad=(n+6)/7*7;if(pad>capacity)return -1;
 for(i=n;i<pad;i++){int j;grid[i].id=164;grid[i].count=0;for(j=0;j<7;j++)grid[i].form[j]=0;}
 return pad;
}
#endif
