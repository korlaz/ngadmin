
#include "lib.h"
#include "network.h"



static const struct timeval default_timeout={.tv_sec=3, .tv_usec=0};



// ---------------------------------------------
struct ngadmin* ngadmin_init (const char *iface) {
 
 struct ngadmin *nga;
 
 
 // allocate main structure
 nga=malloc(sizeof(struct ngadmin));
 memset(nga, 0, sizeof(struct ngadmin));
 
 strncpy(nga->iface, iface, IFNAMSIZ-1);
 
 if ( startNetwork(nga)<0 ) {
  free(nga);
  return NULL;
 }
 
 nga->timeout=default_timeout;
 if ( updateTimeout(nga)<0 ) {
  free(nga);
  return NULL;
 }
 
 
 return nga;
 
}



// ------------------------------------
int ngadmin_close (struct ngadmin *nga) {
 
 if ( nga==NULL ) {
  return ERR_INVARG;
 }
 
 
 stopNetwork(nga);
 free(nga->swi_tab);
 free(nga);
 
 
 return ERR_OK;
 
}



// ---------------------------------------------
int ngadmin_forceInterface (struct ngadmin *nga) {
 
 
 if ( nga==NULL ) {
  return ERR_INVARG;
 }
 
 
 if ( forceInterface(nga)!=0 ) {
  return ERR_NET;
 } else {
  return ERR_OK;
 }
 
}



// --------------------------------------------------------------
int ngadmin_setKeepBroadcasting (struct ngadmin *nga, bool value) {
 
 
 if ( nga==NULL ) {
  return ERR_INVARG;
 }
 
 
 nga->keepbroad=value;
 
 
 return ERR_OK;
 
}



// ------------------------------------------------------------
int ngadmin_setPassword (struct ngadmin *nga, const char *pass) {
 
 if ( nga==NULL ) {
  return ERR_INVARG;
 }
 
 
 strncpy(nga->password, pass, PASSWORD_MAX);
 
 
 return ERR_OK;
 
}



// -------------------------------------------------------------------
int ngadmin_setTimeout (struct ngadmin *nga, const struct timeval *tv) {
 
 int ret=ERR_OK;
 
 
 if ( nga==NULL || tv==NULL ) {
  return ERR_INVARG;
 }
 
 
 nga->timeout=*tv;
 if ( updateTimeout(nga)<0 ) {
  ret=ERR_NET;
 }
 
 
 return ret;
 
}



// -----------------------------------
int ngadmin_scan (struct ngadmin *nga) {
 
 int i;
 List *attr, *swiList;
 struct swi_attr *sa;
 /*
 sent by official win client:
  ATTR_PRODUCT, 
  ATTR_UNK2, 
  ATTR_NAME, 
  ATTR_MAC, 
  ATTR_UNK5, 
  ATTR_IP, 
  ATTR_NETMASK, 
  ATTR_GATEWAY, 
  ATTR_DHCP, 
  ATTR_UNK12, 
  ATTR_FIRM_VER, 
  ATTR_UNK14, 
  ATTR_UNK15, 
  ATTR_END
 */
 static const unsigned short hello[]={
  ATTR_PRODUCT, 
  ATTR_NAME, 
  ATTR_MAC, 
  ATTR_IP, 
  ATTR_NETMASK, 
  ATTR_GATEWAY, 
  ATTR_DHCP, 
  ATTR_FIRM_VER, 
  ATTR_PORTS_COUNT, 
  ATTR_END
 };
 
 
 if ( nga==NULL ) {
  return ERR_INVARG;
 }
 
 free(nga->swi_tab);
 nga->swi_tab=NULL;
 nga->swi_count=0;
 nga->current=NULL;
 
 
 // create attributes for an "hello" request
 attr=createEmptyList();
 for (i=0; ; ++i) {
  pushBackList(attr, newEmptyAttr(hello[i]));
  if ( hello[i]==ATTR_END ) break;
 }
 
 // send request to all potential switches
 i=sendNgPacket(nga, CODE_READ_REQ, attr);
 destroyList(attr, (void(*)(void*))freeAttr);
 if ( i<0 ) {
  return ERR_NET;
 }
 
 
 // try to receive any packets until timeout
 swiList=createEmptyList();
 while ( (attr=recvNgPacket(nga, CODE_READ_REP, NULL, NULL))!=NULL ) {
  sa=malloc(sizeof(struct swi_attr));
  extractSwitchAttributes(sa, attr);
  destroyList(attr, (void(*)(void*))freeAttr);
  pushBackList(swiList, sa);
 }
 
 nga->swi_count=swiList->count;
 nga->swi_tab=convertToArray(swiList, sizeof(struct swi_attr));
 
 
 return ERR_OK;
 
}



// -----------------------------------------------------------------------
const struct swi_attr* ngadmin_getSwitchTab (struct ngadmin *nga, int *nb) {
 
 
 if ( nga==NULL || nb==NULL ) {
  return NULL;
 }
 
 
 *nb=nga->swi_count;
 
 
 return nga->swi_tab;
 
}



// ------------------------------------------------------------------
const struct swi_attr* ngadmin_getCurrentSwitch (struct ngadmin *nga) {
 
 
 if ( nga==NULL ) {
  return NULL;
 }
 
 
 return nga->current;
 
}



// --------------------------------------------
int ngadmin_login (struct ngadmin *nga, int id) {
 
 List *attr;
 int ret=ERR_OK, i;
 struct swi_attr *sa;
 char err;
 unsigned short attr_error;
 
 
 if ( nga==NULL ) {
  return ERR_INVARG;
 } else if ( id<0 || id>=nga->swi_count ) {
  return ERR_BADID;
 }
 
 
 sa=&nga->swi_tab[id];
 nga->current=sa;
 
 attr=createEmptyList();
 pushBackList(attr, newAttr(ATTR_PASSWORD, strlen(nga->password), strdup(nga->password)));
 pushBackList(attr, newEmptyAttr(ATTR_END));
 i=sendNgPacket(nga, CODE_READ_REQ, attr);
 destroyList(attr, (void(*)(void*))freeAttr);
 if ( i<0 || (attr=recvNgPacket(nga, CODE_READ_REP, &err, &attr_error))==NULL ) {
  ret=ERR_NET;
  nga->current=NULL;
  goto end;
 }
 
 
 destroyList(attr, (void(*)(void*))freeAttr);
 if ( err==7 && attr_error==ATTR_PASSWORD ) {
  ret=ERR_BADPASS;
  nga->current=NULL;
  goto end;
 }
 
 
 
 end:
 
 return ret;
 
}



// -------------------------------------------------------------------
int ngadmin_getPortsStatus (struct ngadmin *nga, unsigned char *ports) {
 
 List *attr;
 ListNode *ln;
 struct attr *at;
 int ret=ERR_OK, i;
 char *p;
 
 
 if ( nga==NULL || ports==NULL ) {
  return ERR_INVARG;
 } else if ( nga->current==NULL ) {
  return ERR_NOTLOG;
 }
 
 
 attr=createEmptyList();
 pushBackList(attr, newEmptyAttr(ATTR_PORT_STATUS));
 pushBackList(attr, newEmptyAttr(ATTR_END));
 i=sendNgPacket(nga, CODE_READ_REQ, attr);
 destroyList(attr, (void(*)(void*))freeAttr);
 if ( i<0 || (attr=recvNgPacket(nga, CODE_READ_REP, NULL, NULL))==NULL ) {
  ret=ERR_NET;
  goto end;
 }
 
 for (ln=attr->first; ln!=NULL; ln=ln->next) {
  at=ln->data;
  p=at->data;
  if ( at->attr==ATTR_PORT_STATUS && at->size>=2 && (i=p[0]-1)>=0 && i<nga->current->ports ) {
   ports[i]=p[1];
  }
 }
 
 destroyList(attr, (void(*)(void*))freeAttr);
 
 
 end:
 
 return ret;
 
}



// --------------------------------------------------------
int ngadmin_setName (struct ngadmin *nga, const char *name) {
 
 List *attr;
 int ret=ERR_OK, i;
 char err;
 unsigned short attr_error;
 
 
 if ( nga==NULL ) {
  return ERR_INVARG;
 } else if ( nga->current==NULL ) {
  return ERR_NOTLOG;
 }
 
 
 attr=createEmptyList();
 pushBackList(attr, newAttr(ATTR_PASSWORD, strlen(nga->password), strdup(nga->password)));
 if ( name==NULL ) {
  pushBackList(attr, newEmptyAttr(ATTR_NAME));
 } else {
  pushBackList(attr, newAttr(ATTR_NAME, strlen(name), strdup(name)));
 }
 pushBackList(attr, newEmptyAttr(ATTR_END));
 i=sendNgPacket(nga, CODE_WRITE_REQ, attr);
 destroyList(attr, (void(*)(void*))freeAttr);
 if ( i<0 || (attr=recvNgPacket(nga, CODE_WRITE_REP, &err, &attr_error))==NULL ) {
  ret=ERR_NET;
  goto end;
 }
 
 destroyList(attr, (void(*)(void*))freeAttr);
 if ( err==7 && attr_error==ATTR_PASSWORD ) {
  ret=ERR_BADPASS;
  goto end;
 }
 
 
 // successful, also update local name
 if ( name==NULL ) {
  nga->current->name[0]=0;
 } else {
  strncpy(nga->current->name, name, NAME_SIZE);
 }
 
 
 end:
 
 return ret;
 
}



// ------------------------------------------------------------------------
int ngadmin_getPortsStatistics (struct ngadmin *nga, struct port_stats *ps) {
 
 List *attr;
 ListNode *ln;
 struct attr *at;
 int ret=ERR_OK, i;
 int port;
 
 
 if ( nga==NULL || ps==NULL ) {
  return ERR_INVARG;
 } else if ( nga->current==NULL ) {
  return ERR_NOTLOG;
 }
 
 
 attr=createEmptyList();
 pushBackList(attr, newEmptyAttr(ATTR_PORT_STATISTICS));
 pushBackList(attr, newEmptyAttr(ATTR_END));
 i=sendNgPacket(nga, CODE_READ_REQ, attr);
 destroyList(attr, (void(*)(void*))freeAttr);
 if ( i<0 || (attr=recvNgPacket(nga, CODE_READ_REP, NULL, NULL))==NULL ) {
  ret=ERR_NET;
  goto end;
 }
 
 for (ln=attr->first; ln!=NULL; ln=ln->next) {
  at=ln->data;
  if ( at->attr==ATTR_PORT_STATISTICS && at->size>=49 && (port=(int)(*(char*)at->data)-1)>=0 && port<nga->current->ports ) {
   ps[port].recv=be64toh(*(unsigned long long*)(at->data+1+8*0));
   ps[port].sent=be64toh(*(unsigned long long*)(at->data+1+8*1));
   ps[port].crc=be64toh(*(unsigned long long*)(at->data+1+8*5));
   // all offsets between 2 and 4 inclusive are unknown values
  }
 }
 
 destroyList(attr, (void(*)(void*))freeAttr);
 
 
 end:
 
 return ret;
 
}



// ---------------------------------------------------
int ngadmin_resetPortsStatistics (struct ngadmin *nga) {
 
 List *attr;
 int ret=ERR_OK, i;
 char err;
 unsigned short attr_error;
 
 
 if ( nga==NULL ) {
  return ERR_INVARG;
 } else if ( nga->current==NULL ) {
  return ERR_NOTLOG;
 }
 
 
 attr=createEmptyList();
 pushBackList(attr, newAttr(ATTR_PASSWORD, strlen(nga->password), strdup(nga->password)));
 pushBackList(attr, newByteAttr(ATTR_STATS_RESET, 1));
 pushBackList(attr, newEmptyAttr(ATTR_END));
 i=sendNgPacket(nga, CODE_WRITE_REQ, attr);
 destroyList(attr, (void(*)(void*))freeAttr);
 if ( i<0 || (attr=recvNgPacket(nga, CODE_WRITE_REP, &err, &attr_error))==NULL ) {
  ret=ERR_NET;
  goto end;
 }
 
 destroyList(attr, (void(*)(void*))freeAttr);
 
 if ( err==7 && attr_error==ATTR_PASSWORD ) {
  ret=ERR_BADPASS;
  goto end;
 }
 
 
 
 end:
 
 return ret;
 
}



// ---------------------------------------------------------------
int ngadmin_changePassword (struct ngadmin *nga, const char* pass) {
 
 List *attr;
 int ret=ERR_OK, i;
 char err;
 unsigned short attr_error;
 
 
 if ( nga==NULL || pass==NULL ) {
  return ERR_INVARG;
 } else if ( nga->current==NULL ) {
  return ERR_NOTLOG;
 }
 
 
 attr=createEmptyList();
 pushBackList(attr, newAttr(ATTR_PASSWORD, strlen(nga->password), strdup(nga->password)));
 pushBackList(attr, newAttr(ATTR_NEW_PASSWORD, strlen(pass), strdup(pass)));
 pushBackList(attr, newEmptyAttr(ATTR_END));
 i=sendNgPacket(nga, CODE_WRITE_REQ, attr);
 destroyList(attr, (void(*)(void*))freeAttr);
 if ( i<0 || (attr=recvNgPacket(nga, CODE_WRITE_REP, &err, &attr_error))==NULL ) {
  ret=ERR_NET;
  goto end;
 }
 
 destroyList(attr, (void(*)(void*))freeAttr);
 
 if ( err==7 && attr_error==ATTR_PASSWORD ) {
  ret=ERR_BADPASS;
  goto end;
 }
 
 
 // successful, also update local password
 strncpy(nga->password, pass, PASSWORD_MAX);
 
 
 end:
 
 return ret;
 
}



// ----------------------------------------------------------
int ngadmin_getStormFilterState (struct ngadmin *nga, int *s) {
 
 List *attr;
 ListNode *ln;
 struct attr *at;
 int ret=ERR_OK, i;
 
 
 if ( nga==NULL || s==NULL ) {
  return ERR_INVARG;
 } else if ( nga->current==NULL ) {
  return ERR_NOTLOG;
 }
 
 
 attr=createEmptyList();
 pushBackList(attr, newEmptyAttr(ATTR_STORM_ENABLE));
 pushBackList(attr, newEmptyAttr(ATTR_END));
 i=sendNgPacket(nga, CODE_READ_REQ, attr);
 destroyList(attr, (void(*)(void*))freeAttr);
 if ( i<0 || (attr=recvNgPacket(nga, CODE_READ_REP, NULL, NULL))==NULL ) {
  ret=ERR_NET;
  goto end;
 }
 
 for (ln=attr->first; ln!=NULL; ln=ln->next) {
  at=ln->data;
  if ( at->attr==ATTR_STORM_ENABLE && at->size>=1 ) {
   *s= *(char*)at->data!=0 ;
   break;
  }
 }
 
 destroyList(attr, (void(*)(void*))freeAttr);
 
 
 end:
 
 return ret;
 
}



// ---------------------------------------------------------
int ngadmin_setStormFilterState (struct ngadmin *nga, int s) {
 
 List *attr;
 int ret=ERR_OK, i;
 char err;
 unsigned short attr_error;
 
 
 if ( nga==NULL ) {
  return ERR_INVARG;
 } else if ( nga->current==NULL ) {
  return ERR_NOTLOG;
 }
 
 
 attr=createEmptyList();
 pushBackList(attr, newAttr(ATTR_PASSWORD, strlen(nga->password), strdup(nga->password)));
 pushBackList(attr, newByteAttr(ATTR_STORM_ENABLE, s!=0));
 pushBackList(attr, newEmptyAttr(ATTR_END));
 i=sendNgPacket(nga, CODE_WRITE_REQ, attr);
 destroyList(attr, (void(*)(void*))freeAttr);
 if ( i<0 || (attr=recvNgPacket(nga, CODE_WRITE_REP, &err, &attr_error))==NULL ) {
  ret=ERR_NET;
  goto end;
 }
 
 destroyList(attr, (void(*)(void*))freeAttr);
 
 if ( err==7 && attr_error==ATTR_PASSWORD ) {
  ret=ERR_BADPASS;
  goto end;
 }
 
 
 end:
 
 return ret;
 
}



// ---------------------------------------------------------------
int ngadmin_getStormFilterValues (struct ngadmin *nga, int *ports) {
 
 List *attr;
 ListNode *ln;
 struct attr *at;
 int ret=ERR_OK, i;
 
 
 if ( nga==NULL || ports==NULL ) {
  return ERR_INVARG;
 } else if ( nga->current==NULL ) {
  return ERR_NOTLOG;
 }
 
 
 attr=createEmptyList();
 pushBackList(attr, newEmptyAttr(ATTR_STORM_BITRATE));
 pushBackList(attr, newEmptyAttr(ATTR_END));
 i=sendNgPacket(nga, CODE_READ_REQ, attr);
 destroyList(attr, (void(*)(void*))freeAttr);
 if ( i<0 || (attr=recvNgPacket(nga, CODE_READ_REP, NULL, NULL))==NULL ) {
  ret=ERR_NET;
  goto end;
 }
 
 for (ln=attr->first; ln!=NULL; ln=ln->next) {
  at=ln->data;
  if ( at->attr==ATTR_STORM_BITRATE && at->size>=5 && (i=*(char*)(at->data)-1)>=0 && i<nga->current->ports ) {
   ports[i]=ntohl(*(int*)(1+(char*)at->data));
  }
 }
 
 destroyList(attr, (void(*)(void*))freeAttr);
 
 
 end:
 
 return ret;
 
}



// ---------------------------------------------------------------------
int ngadmin_setStormFilterValues (struct ngadmin *nga, const int *ports) {
 
 List *attr;
 int ret=ERR_OK, i;
 char err;
 unsigned short attr_error;
 char *p;
 
 
 if ( nga==NULL || ports==NULL ) {
  return ERR_INVARG;
 } else if ( nga->current==NULL ) {
  return ERR_NOTLOG;
 }
 
 
 attr=createEmptyList();
 pushBackList(attr, newAttr(ATTR_PASSWORD, strlen(nga->password), strdup(nga->password)));
 
 for (i=0; i<nga->current->ports; ++i) {
  if ( ports[i]>=0 && ports[i]<=11 ) {
   p=malloc(5);
   *p=i+1;
   *(int*)(p+1)=htonl(ports[i]);
   pushBackList(attr, newAttr(ATTR_STORM_BITRATE, 5, p));
  }
 }
 
 pushBackList(attr, newEmptyAttr(ATTR_END));
 i=sendNgPacket(nga, CODE_WRITE_REQ, attr);
 destroyList(attr, (void(*)(void*))freeAttr);
 if ( i<0 || (attr=recvNgPacket(nga, CODE_WRITE_REP, &err, &attr_error))==NULL ) {
  ret=ERR_NET;
  goto end;
 }
 
 destroyList(attr, (void(*)(void*))freeAttr);
 
 if ( err==7 && attr_error==ATTR_PASSWORD ) {
  ret=ERR_BADPASS;
  goto end;
 }
 
 
 end:
 
 return ret;
 
}



// -----------------------------------------------------------
int ngadmin_getBitrateLimits (struct ngadmin *nga, int *ports) {
 
 List *attr;
 ListNode *ln;
 struct attr *at;
 int ret=ERR_OK, i;
 
 
 if ( nga==NULL || ports==NULL ) {
  return ERR_INVARG;
 } else if ( nga->current==NULL ) {
  return ERR_NOTLOG;
 }
 
 
 attr=createEmptyList();
 pushBackList(attr, newEmptyAttr(ATTR_BITRATE_INPUT));
 pushBackList(attr, newEmptyAttr(ATTR_BITRATE_OUTPUT));
 pushBackList(attr, newEmptyAttr(ATTR_END));
 i=sendNgPacket(nga, CODE_READ_REQ, attr);
 destroyList(attr, (void(*)(void*))freeAttr);
 if ( i<0 || (attr=recvNgPacket(nga, CODE_READ_REP, NULL, NULL))==NULL ) {
  ret=ERR_NET;
  goto end;
 }
 
 for (ln=attr->first; ln!=NULL; ln=ln->next) {
  at=ln->data;
  if ( at->attr==ATTR_BITRATE_INPUT && at->size>=5 && (i=*(char*)(at->data)-1)>=0 && i<nga->current->ports ) {
   ports[i*2+0]=ntohl(*(int*)(1+(char*)at->data));
  } else if ( at->attr==ATTR_BITRATE_OUTPUT && at->size>=5 && (i=*(char*)(at->data)-1)>=0 && i<nga->current->ports ) {
   ports[i*2+1]=ntohl(*(int*)(1+(char*)at->data));
  }
 }
 
 destroyList(attr, (void(*)(void*))freeAttr);
 
 
 end:
 
 return ret;
 
}



// -----------------------------------------------------------------
int ngadmin_setBitrateLimits (struct ngadmin *nga, const int *ports) {
 
 List *attr;
 int ret=ERR_OK, i;
 char err;
 unsigned short attr_error;
 char *p;
 
 
 if ( nga==NULL || ports==NULL ) {
  return ERR_INVARG;
 } else if ( nga->current==NULL ) {
  return ERR_NOTLOG;
 }
 
 
 attr=createEmptyList();
 pushBackList(attr, newAttr(ATTR_PASSWORD, strlen(nga->password), strdup(nga->password)));
 
 for (i=0; i<nga->current->ports; ++i) {
  if ( ports[2*i+0]>=0 && ports[2*i+0]<=11 ) {
   p=malloc(5);
   *p=i+1;
   *(int*)(p+1)=htonl(ports[2*i+0]);
   pushBackList(attr, newAttr(ATTR_BITRATE_INPUT, 5, p));
  }
  if ( ports[2*i+1]>=0 && ports[2*i+1]<=11 ) {
   p=malloc(5);
   *p=i+1;
   *(int*)(p+1)=htonl(ports[2*i+1]);
   pushBackList(attr, newAttr(ATTR_BITRATE_OUTPUT, 5, p));
  }
 }
 
 pushBackList(attr, newEmptyAttr(ATTR_END));
 i=sendNgPacket(nga, CODE_WRITE_REQ, attr);
 destroyList(attr, (void(*)(void*))freeAttr);
 if ( i<0 || (attr=recvNgPacket(nga, CODE_WRITE_REP, &err, &attr_error))==NULL ) {
  ret=ERR_NET;
  goto end;
 }
 
 destroyList(attr, (void(*)(void*))freeAttr);
 
 if ( err==7 && attr_error==ATTR_PASSWORD ) {
  ret=ERR_BADPASS;
  goto end;
 }
 
 
 end:
 
 return ret;
 
}



// -------------------------------------------------
int ngadmin_getQOSMode (struct ngadmin *nga, int *s) {
 
 List *attr;
 ListNode *ln;
 struct attr *at;
 int ret=ERR_OK, i;
 
 
 if ( nga==NULL || s==NULL ) {
  return ERR_INVARG;
 } else if ( nga->current==NULL ) {
  return ERR_NOTLOG;
 }
 
 
 attr=createEmptyList();
 pushBackList(attr, newEmptyAttr(ATTR_QOS_TYPE));
 pushBackList(attr, newEmptyAttr(ATTR_END));
 i=sendNgPacket(nga, CODE_READ_REQ, attr);
 destroyList(attr, (void(*)(void*))freeAttr);
 if ( i<0 || (attr=recvNgPacket(nga, CODE_READ_REP, NULL, NULL))==NULL ) {
  ret=ERR_NET;
  goto end;
 }
 
 for (ln=attr->first; ln!=NULL; ln=ln->next) {
  at=ln->data;
  if ( at->attr==ATTR_QOS_TYPE && at->size>=1 ) {
   *s= *(char*)at->data ;
   break;
  }
 }
 
 destroyList(attr, (void(*)(void*))freeAttr);
 
 
 end:
 
 return ret;
 
}


// ------------------------------------------------
int ngadmin_setQOSMode (struct ngadmin *nga, int s) {
 
 List *attr;
 int ret=ERR_OK, i;
 char err;
 unsigned short attr_error;
 
 
 if ( nga==NULL || s<1 || s>2 ) {
  return ERR_INVARG;
 } else if ( nga->current==NULL ) {
  return ERR_NOTLOG;
 }
 
 
 attr=createEmptyList();
 pushBackList(attr, newAttr(ATTR_PASSWORD, strlen(nga->password), strdup(nga->password)));
 pushBackList(attr, newByteAttr(ATTR_QOS_TYPE, s));
 pushBackList(attr, newEmptyAttr(ATTR_END));
 i=sendNgPacket(nga, CODE_WRITE_REQ, attr);
 destroyList(attr, (void(*)(void*))freeAttr);
 if ( i<0 || (attr=recvNgPacket(nga, CODE_WRITE_REP, &err, &attr_error))==NULL ) {
  ret=ERR_NET;
  goto end;
 }
 
 destroyList(attr, (void(*)(void*))freeAttr);
 
 if ( err==7 && attr_error==ATTR_PASSWORD ) {
  ret=ERR_BADPASS;
  goto end;
 }
 
 
 end:
 
 return ret;
 
}



// -------------------------------------------------------
int ngadmin_getQOSValues (struct ngadmin *nga, char *ports) {
 
 List *attr;
 ListNode *ln;
 struct attr *at;
 int ret=ERR_OK, i;
 char *p;
 
 
 if ( nga==NULL || ports==NULL ) {
  return ERR_INVARG;
 } else if ( nga->current==NULL ) {
  return ERR_NOTLOG;
 }
 
 
 attr=createEmptyList();
 pushBackList(attr, newEmptyAttr(ATTR_QOS_CONFIG));
 pushBackList(attr, newEmptyAttr(ATTR_END));
 i=sendNgPacket(nga, CODE_READ_REQ, attr);
 destroyList(attr, (void(*)(void*))freeAttr);
 if ( i<0 || (attr=recvNgPacket(nga, CODE_READ_REP, NULL, NULL))==NULL ) {
  ret=ERR_NET;
  goto end;
 }
 
 for (ln=attr->first; ln!=NULL; ln=ln->next) {
  at=ln->data;
  p=at->data;
  if ( at->attr==ATTR_QOS_CONFIG && at->size>=2 && --p[0]>=0 && p[0]<nga->current->ports ) {
   ports[(int)p[0]]=p[1];
  }
 }
 
 destroyList(attr, (void(*)(void*))freeAttr);
 
 
 end:
 
 return ret;
 
}



// --------------------------------------------------------------
int ngadmin_setQOSValues (struct ngadmin *nga, const char *ports) {
 
 List *attr;
 int ret=ERR_OK, i;
 char err;
 unsigned short attr_error;
 char *p;
 
 
 if ( nga==NULL || ports==NULL ) {
  return ERR_INVARG;
 } else if ( nga->current==NULL ) {
  return ERR_NOTLOG;
 }
 
 
 attr=createEmptyList();
 pushBackList(attr, newAttr(ATTR_PASSWORD, strlen(nga->password), strdup(nga->password)));
 
 for (i=0; i<nga->current->ports; ++i) {
  if ( ports[i]>=PRIO_HIGH && ports[i]<=PRIO_LOW ) {
   p=malloc(2);
   p[0]=i+1;
   p[1]=ports[i];
   pushBackList(attr, newAttr(ATTR_QOS_CONFIG, 2, p));
  }
 }
 
 pushBackList(attr, newEmptyAttr(ATTR_END));
 i=sendNgPacket(nga, CODE_WRITE_REQ, attr);
 destroyList(attr, (void(*)(void*))freeAttr);
 if ( i<0 || (attr=recvNgPacket(nga, CODE_WRITE_REP, &err, &attr_error))==NULL ) {
  ret=ERR_NET;
  goto end;
 }
 
 destroyList(attr, (void(*)(void*))freeAttr);
 
 if ( err==7 && attr_error==ATTR_PASSWORD ) {
  ret=ERR_BADPASS;
  goto end;
 }
 
 
 end:
 
 return ret;
 
}



// --------------------------------------
int ngadmin_restart (struct ngadmin *nga) {
 
 List *attr;
 int ret=ERR_OK, i;
 char err;
 unsigned short attr_error;
 
 
 if ( nga==NULL ) {
  return ERR_INVARG;
 } else if ( nga->current==NULL ) {
  return ERR_NOTLOG;
 }
 
 
 attr=createEmptyList();
 pushBackList(attr, newAttr(ATTR_PASSWORD, strlen(nga->password), strdup(nga->password)));
 pushBackList(attr, newByteAttr(ATTR_RESTART, 1));
 pushBackList(attr, newEmptyAttr(ATTR_END));
 i=sendNgPacket(nga, CODE_WRITE_REQ, attr);
 destroyList(attr, (void(*)(void*))freeAttr);
 if ( i<0 || (attr=recvNgPacket(nga, CODE_WRITE_REP, &err, &attr_error))==NULL ) {
  ret=ERR_NET;
  goto end;
 }
 
 destroyList(attr, (void(*)(void*))freeAttr);
 
 if ( err==7 && attr_error==ATTR_PASSWORD ) {
  ret=ERR_BADPASS;
  goto end;
 }
 
 
 end:
 
 return ret;
 
}



// ---------------------------------------
int ngadmin_defaults (struct ngadmin *nga) {
 
 List *attr;
 int ret=ERR_OK, i;
 char err;
 unsigned short attr_error;
 
 
 if ( nga==NULL ) {
  return ERR_INVARG;
 } else if ( nga->current==NULL ) {
  return ERR_NOTLOG;
 }
 
 
 attr=createEmptyList();
 pushBackList(attr, newAttr(ATTR_PASSWORD, strlen(nga->password), strdup(nga->password)));
 pushBackList(attr, newByteAttr(ATTR_DEFAULTS, 1));
 pushBackList(attr, newEmptyAttr(ATTR_END));
 i=sendNgPacket(nga, CODE_WRITE_REQ, attr);
 destroyList(attr, (void(*)(void*))freeAttr);
 if ( i<0 || (attr=recvNgPacket(nga, CODE_WRITE_REP, &err, &attr_error))==NULL ) {
  ret=ERR_NET;
  goto end;
 }
 
 destroyList(attr, (void(*)(void*))freeAttr);
 
 if ( err==7 && attr_error==ATTR_PASSWORD ) {
  ret=ERR_BADPASS;
  goto end;
 }
 
 
 // successful: delog and clean list
 free(nga->swi_tab);
 nga->swi_tab=NULL;
 nga->swi_count=0;
 nga->current=NULL;
 
 
 end:
 
 return ret;
 
}


