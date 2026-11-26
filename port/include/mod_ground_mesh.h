/* Procedural chipped crust + earth interiors; no imported textures or game RNG. */
#ifndef MOD_GROUND_MESH_H
#define MOD_GROUND_MESH_H
#ifndef GC_MATERIAL
#define GC_MATERIAL(k) ((void)0)
#endif
#ifndef GC_DOMAIN
#define GC_DOMAIN(p,r) ((void)0)
#endif
static float GC_Noise(int seed,int k){unsigned x=(unsigned)seed*747796405u+(unsigned)k*2891336453u+277803737u;x^=x>>16;x*=2246822519u;x^=x>>13;return GC_Float((int)(x&1023u))/1023.f;}
static void GC_Colors(int material,GcVec*top,GcVec*dirt){*dirt=(GcVec){94,67,42};switch(material){case 1:*top=(GcVec){64,96,38};break;case 2:*top=(GcVec){120,116,106};break;case 3:*top=(GcVec){162,132,81};*dirt=(GcVec){129,99,62};break;case 4:*top=(GcVec){210,221,232};*dirt=(GcVec){160,184,209};break;case 8:*top=(GcVec){181,112,105};*dirt=(GcVec){119,64,73};break;case 6:*top=(GcVec){104,62,56};*dirt=(GcVec){83,51,45};break;case 7:*top=(GcVec){69,117,135};*dirt=(GcVec){126,108,84};break;default:*top=(GcVec){111,84,54};}}
static void GC_RockMesh(const GcRock*r,int quality){int i,n=quality==1?4:5;GcVec top,dirt,upper[5],lower[5],ct=r->p,cb=r->p;float size=r->size,phase=GC_Noise(r->seed,1)*6.2831853f;
 GC_Colors(r->surface,&top,&dirt);ct.x+=size*.12f;ct.y-=size*.40f;cb.x-=size*.16f;cb.y+=size*.64f;cb.z+=size*.10f;
 for(i=0;i<n;i++){float a=phase+GC_Float(i)*6.2831853f/GC_Float(n),rad=size*(.73f+.43f*GC_Noise(r->seed,3+i));
  upper[i]=(GcVec){r->p.x+cosf(a)*rad,r->p.y-size*(.14f+.16f*GC_Noise(r->seed,13+i)),r->p.z+sinf(a)*rad};
  lower[i]=(GcVec){r->p.x+cosf(a+.17f)*rad*.70f,r->p.y+size*(.32f+.25f*GC_Noise(r->seed,23+i)),r->p.z+sinf(a+.17f)*rad*.70f};
 }
 for(i=0;i<n;i++){int j=(i+1)%n;float shade=.68f+.28f*GC_Noise(r->seed,33+i);
  GC_MATERIAL(1);GC_MESH_TRI(ct,upper[i],upper[j],top,.88f+.08f*GC_Noise(r->seed,43+i),r->alpha);
  GC_MATERIAL(0);GC_MESH_TRI(upper[i],lower[i],upper[j],dirt,shade,r->alpha);
  GC_MESH_TRI(upper[j],lower[i],lower[j],dirt,shade*.9f,r->alpha);
  GC_MESH_TRI(cb,lower[j],lower[i],dirt,.52f,r->alpha);
 }
}
static GcVec GC_FloorPoint(const GcMark*m,float x,float z,float lift){GcVec p={m->p.x+x,m->p.y-lift,m->p.z+z};p.y-=(m->n.x*x+m->n.z*z)/m->n.y;return p;}
/* A fracture has a shaded seam and raised soil/ice banks; never a flat black ink ribbon. */
static void GC_Fracture(const GcMark*m,float ax,float az,float bx,float bz,float width,float alpha){
 float dx=bx-ax,dz=bz-az,len=sqrtf(dx*dx+dz*dz),h=m->height>.1f?m->height:10.f;GcVec top,dirt,dark;
 GcVec a,b,c,d,al,bl,cl,dl;if(len<.001f)return;GC_Colors(m->surface,&top,&dirt);
 dark=m->surface==4?(GcVec){104,139,173}:m->surface==8?(GcVec){66,28,43}:(GcVec){51,43,32};dx=dx/len*width;dz=dz/len*width;
 a=GC_FloorPoint(m,ax-dz,az+dx,h*.020f);b=GC_FloorPoint(m,ax+dz,az-dx,h*.020f);
 c=GC_FloorPoint(m,bx-dz*.55f,bz+dx*.55f,h*.020f);d=GC_FloorPoint(m,bx+dz*.55f,bz-dx*.55f,h*.020f);
 al=GC_FloorPoint(m,ax-dz*2.4f,az+dx*2.4f,h*.043f);bl=GC_FloorPoint(m,ax+dz*2.4f,az-dx*2.4f,h*.039f);
 cl=GC_FloorPoint(m,bx-dz*1.5f,bz+dx*1.5f,h*.035f);dl=GC_FloorPoint(m,bx+dz*1.5f,bz-dx*1.5f,h*.032f);
 GC_MATERIAL(3);GC_MESH_TRI(a,b,c,dark,1.f,alpha*.72f);GC_MESH_TRI(c,b,d,dark,1.f,alpha*.72f);
 GC_MATERIAL(2);GC_MESH_TRI(al,a,cl,dirt,1.f,alpha);GC_MESH_TRI(cl,a,c,dirt,1.f,alpha);
 GC_MESH_TRI(b,bl,d,dirt,1.f,alpha);GC_MESH_TRI(d,bl,dl,dirt,1.f,alpha);
}
static float GC_RimRadius(int seed,int i,int n,float radius){int a=(i+n-1)%n,b=i%n,c=(i+1)%n;return radius*(.86f+.24f*(GC_Noise(seed,a)+2.f*GC_Noise(seed,b)+GC_Noise(seed,c))*.25f);}
static void GC_MarkMesh(const GcMark*m,int quality){
 int i,n=quality==1?12:20,branches=quality==1?3:5;float h=m->height>.1f?m->height:10.f,scale=GC_TierScale(m->tier),radius=h*.57f*scale;
 float fade=m->age<600?1.f:GC_Float(900-m->age)/300.f,forming=m->age<32?GC_Float(m->age)/32.f:1.f;
 GcVec surface,dirt,center;int seed=m->owner*17;if(!m->tier)return;GC_Colors(m->surface,&surface,&dirt);GC_DOMAIN(m->p,radius);
 center=GC_FloorPoint(m,0,0,h*.018f);
 for(i=0;i<n;i++){
 int j=(i+1)%n;float a=GC_Float(i)*6.2831853f/GC_Float(n),b=GC_Float(j)*6.2831853f/GC_Float(n);
 float ra=GC_RimRadius(seed,i,n,radius),rb=GC_RimRadius(seed,j,n,radius);
 float la=h*scale*(.044f+.038f*GC_Noise(seed,i+20))*forming,lb=h*scale*(.044f+.038f*GC_Noise(seed,j+20))*forming;
 GcVec ia=GC_FloorPoint(m,cosf(a)*ra*.69f,sinf(a)*ra*.69f,h*.018f),ib=GC_FloorPoint(m,cosf(b)*rb*.69f,sinf(b)*rb*.69f,h*.018f);
 GcVec oa=GC_FloorPoint(m,cosf(a)*ra,sinf(a)*ra,la+h*.018f),ob=GC_FloorPoint(m,cosf(b)*rb,sinf(b)*rb,lb+h*.018f);
 GcVec fa=GC_FloorPoint(m,cosf(a)*ra*1.16f,sinf(a)*ra*1.16f,h*.008f),fb=GC_FloorPoint(m,cosf(b)*rb*1.16f,sinf(b)*rb*1.16f,h*.008f);
 /* Shared vertices form an uninterrupted organic mound, original crust outside/exposed soil inside. */
 GC_MATERIAL(2);GC_MESH_TRI(center,ia,ib,dirt,1.f,fade*forming);
 GC_MATERIAL(0);GC_MESH_TRI(ia,oa,ib,dirt,1.f,fade);GC_MESH_TRI(ib,oa,ob,dirt,1.f,fade);
 GC_MATERIAL(1);GC_MESH_TRI(oa,fa,ob,surface,1.f,fade);GC_MESH_TRI(ob,fa,fb,surface,1.f,fade);
 }
 for(i=0;i<branches;i++){
 float a=GC_Float(i)*6.2831853f/GC_Float(branches)+GC_Noise(seed,i+50)*.55f;
 float root=.07f+.08f*GC_Noise(seed,i+70);float px=cosf(a)*radius*root,pz=sinf(a)*radius*root;int k,steps=quality==1?2:3;
 for(k=1;k<=steps;k++){float bend=a+(GC_Noise(seed,i*9+k+80)-.5f)*.40f,r=radius*(.12f+.19f*GC_Float(k));
 float x=cosf(bend)*r,z=sinf(bend)*r;GC_Fracture(m,px,pz,x,z,h*scale*.009f,fade*forming);
 if(k==steps-1&&i%2==0){float fork=bend+.35f+.3f*GC_Noise(seed,i+100);GC_Fracture(m,px,pz,cosf(fork)*r,sinf(fork)*r,h*scale*.005f,fade*forming);}
 px=x;pz=z;}
 }
}
#endif
