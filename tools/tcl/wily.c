/*	$Header: /u/cvs/wtcl/wily.c,v 1.2 1996/11/12 23:33:58 cvs Exp $	*/

#include <u.h>
#include <libc.h>
#include <msg.h>
#include <string.h>
#include <tcl.h>

/*
 *	dispatcher within Wily() routine
 */

/* NEW */
typedef int(WCmdProc)(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);

typedef struct {
  char *name;
  WCmdProc *proc; // This now expects the Object-based signature
} WCmd;
/*
typedef struct WCmd WCmd;
struct WCmd {
  char *name;
  int (*proc)(Tcl_Interp *, int, char **);
};
*/

/*
 *	the handle to wily.  should be 0 if not connected
 */
static Handle *h;

/*
 *	the last event fetched by rpc_event() used
 *	by bounce -- we can only bounce if we haveevent
 */
static bool haveevent = false;
static Msg m;

static bool iscon(void) {
  if (h == 0 || rpc_isconnected(h) == false) {
    h = 0;
    return false;
  }
  return true;
}

/*
 *	check args etc for Wily() sub command.
 *	check that argc == argswanted.
 *	if id != 0 then check that we have an id
 *	and return it as *id.
 *	check we are connected to wily.
 *	if anything fails then set the result string
 */

static int get_id(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[],
               int argswanted, const char *usage, int *id) {

    if (objc < argswanted) {
        Tcl_WrongNumArgs(interp, 1, objv, usage);
        return TCL_ERROR;
    }

    if (!iscon()) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("not connected", -1));
        return TCL_ERROR;
    }

    if (id != NULL) {
        int n;
        if (Tcl_GetIntFromObj(interp, objv[1], &n) != TCL_OK) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("argument is not a valid ID", -1));
            return TCL_ERROR;
        }
        *id = n;
    }

    return TCL_OK;
}


static int wrpc_init(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
  int fd;

  if (objc > 1) {
    Tcl_SetResult(interp, "init requires no arguments", TCL_STATIC);
    return TCL_ERROR;
  }

  fd = client_connect();
  if (fd < 0) {
    Tcl_SetResult(interp, "can't connect", TCL_STATIC);
    return TCL_ERROR;
  }
  h = rpc_init(fd);
  if (h == 0) {
    Tcl_SetResult(interp, "can't init", TCL_STATIC);
    return TCL_ERROR;
  }
  return TCL_OK;
}

static int wrpc_isconnected(Tcl_Interp *interp, int objc,
                            Tcl_Obj *const objv[]) {
  if (objc > 1) {
    Tcl_AppendResult(interp, "isconnected requires no arguments", TCL_STATIC);
    return TCL_ERROR;
  }
  if (iscon() == false) {
    Tcl_SetResult(interp, "0", TCL_STATIC);
    h = 0;
  } else
    Tcl_SetResult(interp, "1", TCL_STATIC);
  return TCL_OK;
}

static int wrpc_list(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
  char *p;
  char *emsg;
  char *t;
  char *s;

  if (objc > 1) {
    Tcl_AppendResult(interp, "list requires no arguments", TCL_STATIC);
    return TCL_ERROR;
  }
  emsg = rpc_list(h, &p);
  if (emsg != 0) {
    Tcl_SetResult(interp, emsg, TCL_VOLATILE);
    return TCL_ERROR;
  }

  /*
   *	we trust p to be in a reasonable condition
   */
  t = strtok(p, "\n");
  while (t != 0 && (s = strchr(t, '\t')) != 0) {
    s++;
    Tcl_AppendElement(interp, s);
    t = strtok(0, "\n");
  }
  free(p);
  return TCL_OK;
}

static int wrpc_name(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
  Id id;
  char *p;
  char *emsg;
  char *t;
  char *s;

  if (get_id(interp, objc, objv, 2, "name needs id", &id) !=
      TCL_OK)
    return TCL_ERROR;

  emsg = rpc_list(h, &p);
  if (emsg != 0) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(emsg, -1));
    return TCL_ERROR;
  }

  char *searchId = Tcl_GetString(objv[1]);

  t = strtok(p, "\n");
  while (t != 0 && (s = strchr(t, '\t')) != 0) {
    *s++ = '\0';

    if (strcmp(searchId, s) == 0) {
      Tcl_SetObjResult(interp, Tcl_NewStringObj(t, -1));
      free(p);
      return TCL_OK;
    }
    t = strtok(0, "\n");
  }

  Tcl_SetObjResult(interp, Tcl_NewStringObj("id not found", -1));
  free(p);
  return TCL_ERROR;
}

static int wrpc_attach(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
  Id id;
  char *emsg;

  if (get_id(interp, objc, objv, 2, "name needs id", &id) !=
      TCL_OK)
    return TCL_ERROR;

  emsg = rpc_attach(h, (Id)id, WEexec | WEgoto | WEdestroy | WEreplace);

  if (emsg != 0) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(emsg, -1));
    return TCL_ERROR;
  }

  return TCL_OK;
}

static int wrpc_new(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
  char *filename;
  Id id;

  if (objc < 2) {
    Tcl_WrongNumArgs(interp, 1, objv, "filename");
    return TCL_ERROR;
  }

  filename = Tcl_GetString(objv[1]);

  char *emsg = rpc_new(h, filename, &id, 0);
  if (emsg != 0) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj(emsg, -1));
    return TCL_ERROR;
  }

  Tcl_SetObjResult(interp, Tcl_NewLongObj((long)id));
  return TCL_OK;
}

static int wrpc_setname(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
  Id id;
  char *emsg;

  if (get_id(interp, objc, objv, 3, "setname needs id and new_text", &id) !=
      TCL_OK)
    return TCL_ERROR;

  //emsg = rpc_setname(h, id, Tcl_GetString(objv[2])); // XXX
  emsg = rpc_setname(h, id, Tcl_GetString(objv[2])); // XXX
  if (emsg != 0) {
    Tcl_SetResult(interp, emsg, TCL_VOLATILE);
    return TCL_ERROR;
  }
  return TCL_OK;
}

static int wrpc_settools(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
  Id id;
  char *emsg;

  if (get_id(interp, objc, objv, 3, "settools needs id and new text", &id) !=
      TCL_OK)
    return TCL_ERROR;

  //emsg = rpc_settools(h, id, Tcl_GetString(objv[2])); // XXX
  emsg = rpc_setname(h, id, Tcl_GetString(objv[2])); // XXX
  if (emsg != 0) {
    Tcl_SetResult(interp, emsg, TCL_VOLATILE);
    return TCL_ERROR;
  }
  return TCL_OK;
}

static int wrpc_read(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
  Id id;
  char *emsg;
  Range r;
  int p0, p1;
  unsigned char *p; // Use unsigned char for binary data
  int bytes_to_read;

  if (objc != 4) {
    Tcl_WrongNumArgs(interp, 1, objv, "id begin end");
    return TCL_ERROR;
  }
  if (get_id(interp, objc, objv, 3, "settools needs id begin and end", &id) !=
      TCL_OK)
    return TCL_ERROR;

  if ( /* Tcl_GetLongFromObj(interp, objv[1], &id) != TCL_OK || */
      Tcl_GetIntFromObj(interp, objv[2], &p0) != TCL_OK ||
      Tcl_GetIntFromObj(interp, objv[3], &p1) != TCL_OK) {
    return TCL_ERROR;
  }

  r.p0 = p0;
  r.p1 = p1;
  bytes_to_read = UTFmax * RLEN(r);

  if (r.p0 < 0 || r.p1 < 0 || r.p0 > r.p1) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj("strange numbers", -1));
    return TCL_ERROR;
  }

  p = (unsigned char *)malloc(bytes_to_read);
  if (p == NULL) {
    Tcl_SetObjResult(interp, Tcl_NewStringObj("malloc fails", -1));
    return TCL_ERROR;
  }

  emsg = rpc_read(h, (Id)id, r, (char *)p);
  if (emsg != 0) {
    free(p);
    Tcl_SetObjResult(interp, Tcl_NewStringObj(emsg, -1));
    return TCL_ERROR;
  }

  Tcl_SetObjResult(interp, Tcl_NewByteArrayObj(p, bytes_to_read));
  free(p);
  return TCL_OK;
}

static int wrpc_replace(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
  Id id;
  char *emsg;
  Range r;
  int n;

  if (get_id(interp, objc, objv, 5, "replace needs id, begin, end and new text",
          &id) != TCL_OK)
    return TCL_ERROR;

  r.p0 =
      Tcl_GetInt(interp, Tcl_GetString(objv[2]), &n) != TCL_OK ? -1 : n; // XXX
  r.p1 =
      Tcl_GetInt(interp, Tcl_GetString(objv[3]), &n) != TCL_OK ? -1 : n; // XXX

  if (r.p0 < 0 || r.p1 < 0 || r.p0 > r.p1) {
    Tcl_SetResult(interp, "strange numbers", TCL_STATIC);
    return TCL_ERROR;
  }

  emsg = rpc_replace(h, id, r, Tcl_GetString(objv[4])); // XXX
  if (emsg != 0) {
    Tcl_SetResult(interp, emsg, TCL_VOLATILE);
    return TCL_ERROR;
  }
  return TCL_OK;
}

static int wrpc_exec(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
  Id id;
  char *emsg;

  if (get_id(interp, objc, objv, 3, "exec needs id and command", &id) != TCL_OK)
    return TCL_ERROR;

  emsg = rpc_exec(h, id, Tcl_GetString(objv[2])); // XXX
  if (emsg != 0) {
    Tcl_SetResult(interp, emsg, TCL_VOLATILE);
    return TCL_ERROR;
  }
  return TCL_OK;
}

static int wrpc_goto(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
  Id id;
  char *emsg;
  Range r;
  int b;

  if (get_id(interp, objc, objv, 4, "goto needs id, string and flag", &id) !=
      TCL_OK)
    return TCL_ERROR;

  // XXX
  if (Tcl_GetBoolean(interp, Tcl_GetString(objv[3]), &b) != TCL_OK)
    b = 0;

  // XXX
  emsg = rpc_goto(h, &id, &r, Tcl_GetString(objv[2]), b != 0 ? true : false);
  if (emsg != 0) {
    Tcl_SetResult(interp, emsg, TCL_VOLATILE);
    return TCL_ERROR;
  }

  Tcl_Obj *listPtr = Tcl_NewListObj(0, NULL);
  Tcl_ListObjAppendElement(interp, listPtr, Tcl_NewLongObj((long)id));
  Tcl_ListObjAppendElement(interp, listPtr, Tcl_NewLongObj((long)r.p0));
  Tcl_ListObjAppendElement(interp, listPtr, Tcl_NewLongObj((long)r.p1));
  Tcl_SetObjResult(interp, listPtr);
  return TCL_OK;
}

static int wrpc_length(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
  Id id;
  char *emsg;
  Range r;

  if (get_id(interp, objc, objv, 2, "length needs id", &id) != TCL_OK)
    return TCL_ERROR;

  emsg = rpc_goto(h, &id, &r, ":$", false);
  if (emsg != 0) {
    Tcl_SetResult(interp, emsg, TCL_VOLATILE);
    return TCL_ERROR;
  }

  Tcl_SetObjResult(interp, Tcl_NewLongObj((long)r.p0));
  return TCL_OK;
}

static int wrpc_wouldblock(Tcl_Interp *interp, int objc,
                           Tcl_Obj *const objv[]) {
  if (get_id(interp, objc, objv, 1, "wouldblock needs no args", 0) != TCL_OK)
    return TCL_ERROR;

  Tcl_SetResult(interp, rpc_wouldblock(h) == false ? "1" : "0", TCL_STATIC);
  return TCL_OK;
}

static int wrpc_event(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {

  if (get_id(interp, objc, objv, 1, "event needs no args", 0) != TCL_OK)
    return TCL_ERROR;

  if (rpc_event(h, &m) == -1) {
    Tcl_SetResult(interp, "event fails", TCL_STATIC);
    return TCL_ERROR;
  }
  haveevent = true;

  Tcl_Obj *listPtr = Tcl_NewListObj(0, NULL);
  switch (m.t) {
  case WEexec:
    // sprintf(interp->result, "WMexec\t%ld\t%s", (long)m.w, m.s);
    listPtr = Tcl_NewListObj(0, NULL);
    Tcl_ListObjAppendElement(interp, listPtr, Tcl_NewLongObj((long)m.w));
    Tcl_ListObjAppendElement(interp, listPtr, Tcl_NewStringObj((char*)m.s, -1 ));
    Tcl_SetObjResult(interp, listPtr);
    break;
  case WEgoto:
    // sprintf(interp->result, "WMgoto\t%ld\t%ld\t%ld\t%s", (long)m.w, m.r.p0,
    // m.r.p1, m.s);
    listPtr = Tcl_NewListObj(0, NULL);
    Tcl_ListObjAppendElement(interp, listPtr, Tcl_NewLongObj((long)m.w));
    Tcl_ListObjAppendElement(interp, listPtr, Tcl_NewLongObj((long)m.r.p0));
    Tcl_ListObjAppendElement(interp, listPtr, Tcl_NewLongObj((long)m.r.p1));
    Tcl_ListObjAppendElement(interp, listPtr, Tcl_NewLongObj((long)m.s));
    Tcl_SetObjResult(interp, listPtr);
    break;
  case WEdestroy:
    //  sprintf(interp->result, "WMdestory\t%ld", (long)m.w);
    Tcl_SetObjResult(interp, Tcl_NewLongObj((long)m.w));
    break;
  case WEreplace:
    // sprintf(interp->result, "WMreplace\t%ld\t%ld\t%ld\t%s", (long)m.w,
    // m.r.p0, m.r.p1, m.s);
    listPtr = Tcl_NewListObj(0, NULL);
    Tcl_ListObjAppendElement(interp, listPtr, Tcl_NewLongObj((long)m.w));
    Tcl_ListObjAppendElement(interp, listPtr, Tcl_NewLongObj((long)m.r.p0));
    Tcl_ListObjAppendElement(interp, listPtr, Tcl_NewLongObj((long)m.r.p1));
    Tcl_ListObjAppendElement(interp, listPtr, Tcl_NewLongObj((long)m.s));
    Tcl_SetObjResult(interp, listPtr);
    break;
  default:
    Tcl_SetResult(interp, "unknown message type", TCL_STATIC);
    return TCL_ERROR;
  }
  return TCL_OK;
}

static int wrpc_bounce(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
  if (get_id(interp, objc, objv, 1, "bounce needs no args", 0) != TCL_OK)
    return TCL_ERROR;

  if (haveevent != true) {
    Tcl_SetResult(interp, "nothing to bounce", TCL_STATIC);
    return TCL_ERROR;
  }

  if (rpc_bounce(h, &m) != 0) {
    Tcl_SetResult(interp, "rpc_bounce fails", TCL_STATIC);
    return TCL_ERROR;
  }
  return TCL_OK;
}

static WCmd cmds[] = {
    {"init", wrpc_init},     {"isconnected", wrpc_isconnected},
    {"list", wrpc_list},     {"new", wrpc_new},
    {"attach", wrpc_attach}, {"setname", wrpc_setname},
    {"name", wrpc_name},     {"settools", wrpc_settools},
    {"read", wrpc_read},     {"replace", wrpc_replace},
    {"exec", wrpc_exec},     {"goto", wrpc_goto},
    {"length", wrpc_length}, {"wouldblock", wrpc_wouldblock},
    {"event", wrpc_event},   {"bounce", wrpc_bounce},
};

static int Wily(ClientData clientdata, Tcl_Interp *interp, int objc,
                Tcl_Obj *const objv[]) {
  WCmd *p;
  char *cmdName = Tcl_GetString(objv[0]); // The name "wily"

  if (objc > 1) {
    char *subName = Tcl_GetString(objv[1]); // The subcommand (e.g. "new")
    for (p = cmds; p < cmds + (sizeof(cmds) / sizeof(cmds[0])); p++) {
      if (strcmp(subName, p->name) == 0) {
        return (*p->proc)(interp, objc - 1, objv + 1);
      }
    }
  }

  Tcl_Obj *res = Tcl_NewStringObj("usage: ", -1);
  Tcl_AppendStringsToObj(res, cmdName, " ", NULL);
  for (p = cmds; p < cmds + (sizeof(cmds) / sizeof(cmds[0])); p++) {
    Tcl_AppendStringsToObj(res, "[", p->name, "] ", NULL);
  }
  Tcl_SetObjResult(interp, res);
  return TCL_ERROR;
}

extern int Wily_Init(Tcl_Interp *interp) {
  // Tcl_CreateCommand(interp, "wily", Wily, (ClientData)0, (Tcl_CmdDeleteProc
  // *)0);
  Tcl_CreateObjCommand(interp, "wily", Wily, (ClientData)0, NULL);
  return TCL_OK;
}
