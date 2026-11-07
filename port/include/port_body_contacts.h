/* Native melee contacts use object IDs, including late Story reserves and effect models. */
#ifndef PORT_BODY_CONTACTS_H
#define PORT_BODY_CONTACTS_H
#define PORT_BODY_OBJECTS 32
extern unsigned int gPortBodyTouch[PORT_BODY_OBJECTS];
static inline int Port_BodyObjectValid(int id){return (unsigned)id<PORT_BODY_OBJECTS;}
static inline unsigned int Port_BodyObjectBit(int id){return Port_BodyObjectValid(id)?1u<<(unsigned)id:0u;}
#endif
