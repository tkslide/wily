#define PY_SSIZE_T_CLEAN
#include <u.h>
#include <Python.h>
#include <libc.h>
#include <msg.h>

static PyObject *ErrorObject;

typedef struct {
  PyObject_HEAD Handle *h; // The Wily RPC handle
} WilyConnection;

int convert_to_id(PyObject *obj, Id *id) {
  long val = PyLong_AsLong(obj);
  if (PyErr_Occurred())
    return -1;
  *id = (Id)val;
  return 0;
}

static int WilyConnection_init(WilyConnection *self, PyObject *args,
                               PyObject *kwds) {
  int fd;
  fd = client_connect();
  if (fd < 0) {
    PyErr_SetString(ErrorObject, "Could not open Wily RPC handle.");
    return -1;
  }

  // Initialize the Wily RPC handle using the file descriptor
  self->h = rpc_init(fd);
  if (!self->h) {
    PyErr_SetString(ErrorObject, "Could not initialize Wily RPC handle.");
    return -1;
  }
  return 0;
}

static char con_list__doc__[] =
    "List currently open windows list() Returns list of (name, id) tuples "
    "representing currently open windows";

static PyObject *wily_list(WilyConnection *self, PyObject *Py_UNUSED(ignored)) {
  char *buf;
  char *err;

  if ((err = rpc_list(self->h, &buf))) {
    PyErr_SetString(PyExc_RuntimeError, err);
    return NULL;
  }

  PyObject *py_list = PyList_New(0);
  char *line_saveptr;
  char *line = strtok_r(buf, "\n", &line_saveptr);

  while (line != NULL) {
    char *field_saveptr;
    char *str_val = strtok_r(line, "\t", &field_saveptr);
    char *long_val_str = strtok_r(NULL, "\t", &field_saveptr);

    if (str_val && long_val_str) {
      // Create tuple: (string, long)
      PyObject *item_str = PyUnicode_FromString(str_val);
      PyObject *item_long = PyLong_FromLong(atol(long_val_str));

      PyObject *inner_tuple = PyTuple_Pack(2, item_str, item_long);

      PyList_Append(py_list, inner_tuple);

      Py_DECREF(item_str);
      Py_DECREF(item_long);
      Py_DECREF(inner_tuple);
    }

    line = strtok_r(NULL, "\n", &line_saveptr);
  }

  free(buf);
  return py_list;
}

static char con_win__doc__[] =
    "open a window win(name:string, isBackedUp:integer) Returns an integer "
    "window identifier, to be used for later operations on the window ";

static PyObject *wily_win(WilyConnection *self, PyObject *args) {
  char *name;
  int backup;
  Id id;
  char *err;

  if (!PyArg_ParseTuple(args, "sp", &name, &backup)) {
    return NULL;
  }

  /*
   * rpc_new(Handle*, char *name, Id *id, ushort backup)
   * If 'name' already exists, Wily opens it; otherwise, it creates it.
   * 'backup' determines if Wily tracks dirty state/backups.
   */
  if ((err = rpc_new(self->h, name, &id, (ushort)backup))) {
    PyErr_SetString(ErrorObject, err);
    return NULL;
  }

  // Return the new window ID as a Python integer
  return PyLong_FromLong((long)id);
}

static char con_event__doc__[] =
    "event() returns an event tuple (w, t, r0, r1, s), whose fields are: w: "
    "window identifier t: event type r0, r1: affected range s: string The "
    "meaning (if any) of the values of r0, r1 and s depend on the event type "
    "'t' ";

static PyObject *wily_event(WilyConnection *self, PyObject *args) {
  Msg m;
  char *err;
  PyObject *o;

  if (!PyArg_ParseTuple(args, ""))
    return NULL;
  if (rpc_event(self->h, &m)) {
    err = "rpc_event error";
    PyErr_SetString(ErrorObject, err);
    return NULL;
  }
  o = Py_BuildValue("(iills)", m.w, m.t, m.r.p0, m.r.p1, m.s);
  free(m.s);
  return o;
}

static char con_eventwouldblock__doc__[] =
    "eventwouldblock() If eventwouldblock() returns true, calling event() "
    "might have to wait.  If eventwouldblock() returns false, calling event() "
    "would return immediately because an event is already queued up and "
    "waiting";

static PyObject *wily_eventwouldblock(WilyConnection *self, PyObject *args) {
  if (!PyArg_ParseTuple(args, ""))
    return NULL;
  return Py_BuildValue("b", rpc_wouldblock(self->h));
}

static char con_bounce__doc__[] = "bounce(tuple) Called with an event tuple as "
                                  "returned by event().  Returns None";

static PyObject *wily_bounce(WilyConnection *self, PyObject *args) {
  Msg m;
  char *err;

  if (!PyArg_ParseTuple(args, "(iills)", &m.w, &m.t, &m.r.p0, &m.r.p1, &m.s))
    return NULL;
  if (rpc_bounce(self->h, &m)) {
    err = "bounce err";
    PyErr_SetString(ErrorObject, err);
    return NULL;
  }
  Py_INCREF(Py_None);
  return Py_None;
}

static char con_attach__doc__[] =
    "attach(w:integer, mask:integer) 'w' is a window identifier as obtained by "
    "new() or list().  'mask' is a bitmask of event types. Sets the mask of "
    "events to be sent to us.";

static PyObject *wily_attach(WilyConnection *self, PyObject *args) {

  Id id;
  int mask;
  char *err;

  if (!PyArg_ParseTuple(args, "ii", &id, &mask)) {
    return NULL;
  }
  if ((err = rpc_attach(self->h, id, (ushort)mask))) {
    PyErr_SetString(ErrorObject, err);
    return NULL;
  }
  Py_INCREF(Py_None);
  return Py_None;
}

static char con_setname__doc__[] =
    "setname(w:integer, s:string) Set w's name to 's'";

static PyObject *wily_setname(WilyConnection *self, PyObject *args) {
  char *err;
  char *name;
  Id id;

  if (!PyArg_ParseTuple(args, "is", &id, &name))
    return NULL;
  if ((err = rpc_setname(self->h, id, name))) {
    PyErr_SetString(ErrorObject, err);
    return NULL;
  }
  Py_INCREF(Py_None);
  return Py_None;
}

static char con_getname__doc__[] =
    "getname(w:integer) : string Return the name currently visible in w's tag";

static PyObject *wily_getname(WilyConnection *self, PyObject *args) {
  char *err;
  char *name;
  Id id;

  if (!PyArg_ParseTuple(args, "i", &id))
    return NULL;
  if ((err = rpc_getname(self->h, id, &name))) {
    PyErr_SetString(ErrorObject, err);
    return NULL;
  }
  return Py_BuildValue("s", name);
}

static char con_settools__doc__[] =
    "settools(w:integer, s:string) Set w's tools to 's'";

static PyObject *wily_settools(WilyConnection *self, PyObject *args) {
  char *err;
  char *name;
  Id id;

  if (!PyArg_ParseTuple(args, "is", &id, &name))
    return NULL;
  if ((err = rpc_settools(self->h, id, name))) {
    PyErr_SetString(ErrorObject, err);
    return NULL;
  }
  Py_INCREF(Py_None);
  return Py_None;
}

static char con_gettools__doc__[] = "gettools(w:integer) : string Return the "
                                    "tools currently visible in w's tag";

static PyObject *wily_gettools(WilyConnection *self, PyObject *args) {
  char *err;
  char *tools;
  Id id;

  if (!PyArg_ParseTuple(args, "i", &id))
    return NULL;
  if ((err = rpc_gettools(self->h, id, &tools))) {
    PyErr_SetString(ErrorObject, err);
    return NULL;
  }
  return Py_BuildValue("s", tools);
}

static char con_length__doc__[] =
    "length(w:integer) : string Return the length of w";

static PyObject *wily_length(WilyConnection *self, PyObject *args) {
  char *err;
  char *name;
  Id id;
  Range r;
  long from, to;
  int flag;
  if (!PyArg_ParseTuple(args, "i", &id, &from, &to, &name, &flag))
    return NULL;
  r.p0 = from;
  r.p1 = to;
  if ((err = rpc_goto(self->h, &id, &r, ":$", false))) {
    PyErr_SetString(PyExc_ConnectionError, err);
    return NULL;
  }
  return PyLong_FromLong((long)r.p1);
}

static char con_read__doc__[] =
    "read(w:integer, from:integer, to:integer) returns a (UTF) string";

static PyObject *wily_read(WilyConnection *self, PyObject *args) {
  int from, to;
  Id id;
  Range r;
  char *buf, *err;
  PyObject *retval;

  if (!PyArg_ParseTuple(args, "iii", &id, &from, &to))
    return NULL;
  r.p0 = from;
  r.p1 = to;
  buf = malloc(UTFmax * (r.p1 - r.p0));
  if (!buf) {
    PyErr_NoMemory();
    return NULL;
  }
  err = rpc_read(self->h, id, r, buf);

  if (err) {
    PyErr_SetString(ErrorObject, err);
    retval = NULL;
  } else {
    retval = Py_BuildValue("s", buf);
  }
  free(buf);
  return retval;
}

static char con_replace__doc__[] =
    "replace(w:integer, from:integer, to:integer, s:string) replace the text "
    "in 'w' from 'from' to 'to' with 's'";

static PyObject *wily_replace(WilyConnection *self, PyObject *args) {
  char *err;
  int p0, p1;
  char *replace;
  Id id;
  Range r;

  if (!PyArg_ParseTuple(args, "iiis", &id, &p0, &p1, &replace))
    return NULL;
  if (p0 > p1 || p1 < 0 || p0 < 0) {
    PyErr_SetString(ErrorObject, "range out of bounds");
    return NULL;
  }
  r.p0 = p0;
  r.p1 = p1;

  if ((err = rpc_replace(self->h, id, r, replace))) {
    PyErr_SetString(ErrorObject, err);
    return NULL;
  }

  Py_INCREF(Py_None);
  return Py_None;
}

static char con_run__doc__[] = "run(w:integer, s:string) has the same effect "
                               "as sweeping 's' with B2  in window 'w'";

static PyObject *wily_run(WilyConnection *self, PyObject *args) {
  char *err;
  char *name;
  Id id;

  if (!PyArg_ParseTuple(args, "is", &id, &name))
    return NULL;
  if ((err = rpc_exec(self->h, id, name))) {
    PyErr_SetString(ErrorObject, err);
    return NULL;
  }
  Py_INCREF(Py_None);
  return Py_None;
}

static char con_goto__doc__[] =
    "goto(w:integer, from:long, to:long, s:string, flag:integer)has the same "
    "effect as sweeping 's' with B3  in window 'w',and starting any search at "
    "range(from,to), except that we onlywarp and select text if 'flag' is "
    "set.Returns a tuple (w:integer, from:long, to:long),which represents the "
    "window and selection that wasopened.";

static PyObject *wily_goto(WilyConnection *self, PyObject *args) {
  char *err;
  char *name;
  Id id;
  Range r;
  long from, to;
  int flag;
  if (!PyArg_ParseTuple(args, "illsi", &id, &from, &to, &name, &flag))
    return NULL;
  r.p0 = from;
  r.p1 = to;
  if ((err = rpc_goto(self->h, &id, &r, name, flag))) {
    PyErr_SetString(PyExc_ConnectionError, err);
    return NULL;
  }
  return Py_BuildValue("(ill)", id, r.p0, r.p1);
}

static PyMethodDef WilyConnection_methods[] = {
    {"attach", (PyCFunction)wily_attach, METH_VARARGS, con_attach__doc__},
    {"bounce", (PyCFunction)wily_bounce, METH_VARARGS, con_bounce__doc__},
    {"event", (PyCFunction)wily_event, METH_VARARGS, con_event__doc__},
    {"eventwouldblock", (PyCFunction)wily_eventwouldblock, METH_VARARGS,
     con_eventwouldblock__doc__},
    {"getname", (PyCFunction)wily_getname, METH_VARARGS, con_getname__doc__},
    {"gettools", (PyCFunction)wily_gettools, METH_VARARGS, con_gettools__doc__},
    {"goto", (PyCFunction)wily_goto, METH_VARARGS, con_goto__doc__},
    {"list", (PyCFunction)wily_list, METH_NOARGS, con_list__doc__},
    {"read", (PyCFunction)wily_read, METH_VARARGS, con_read__doc__},
    {"replace", (PyCFunction)wily_replace, METH_VARARGS, con_replace__doc__},
    {"run", (PyCFunction)wily_run, METH_VARARGS, con_run__doc__},
    {"setname", (PyCFunction)wily_setname, METH_VARARGS, con_setname__doc__},
    {"settools", (PyCFunction)wily_settools, METH_VARARGS, con_settools__doc__},
    {"win", (PyCFunction)wily_win, METH_VARARGS, con_win__doc__},
    {"length", (PyCFunction)wily_length, METH_VARARGS, con_length__doc__},
    {NULL, NULL, 0, NULL}};

static PyTypeObject WilyConnectionType = {
    PyVarObject_HEAD_INIT(NULL, 0).tp_name = "wily.Connection",
    .tp_doc = "Wily Editor Connection",
    .tp_basicsize = sizeof(WilyConnection),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = PyType_GenericNew,
    .tp_init = (initproc)WilyConnection_init,
    .tp_methods = WilyConnection_methods,
};

static struct PyModuleDef wilymodule = {PyModuleDef_HEAD_INIT,
                                        "wily",
                                        "Python interface for Wily RPC.",
                                        -1,
                                        NULL,
                                        NULL,
                                        NULL,
                                        NULL,
                                        NULL};

PyMODINIT_FUNC PyInit_wily(void) {
  PyObject *m;
  if (PyType_Ready(&WilyConnectionType) < 0)
    return NULL;

  m = PyModule_Create(&wilymodule);
  if (m == NULL)
    return NULL;
  ErrorObject = PyErr_NewException("wily.error", NULL, NULL);
  Py_XINCREF(ErrorObject);
  if (PyModule_AddObject(m, "error", ErrorObject) < 0) {
    Py_XDECREF(ErrorObject);
    Py_DECREF(m);
    return NULL;
  }

  /* Add symbolic constants using PyModule_AddIntConstant for cleaner code */
  PyModule_AddIntConstant(m, "GOTO", WEgoto);
  PyModule_AddIntConstant(m, "EXEC", WEexec);
  PyModule_AddIntConstant(m, "DESTROY", WEdestroy);
  PyModule_AddIntConstant(m, "REPLACE", WEreplace);

  Py_INCREF(&WilyConnectionType);
  if (PyModule_AddObject(m, "Connection", (PyObject *)&WilyConnectionType) <
      0) {
    Py_DECREF(&WilyConnectionType);
    Py_DECREF(m);
    return NULL;
  }

  return m;
}
// vim: ts=4 sw=4 ft=c expandtab :
