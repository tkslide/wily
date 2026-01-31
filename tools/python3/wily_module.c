#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <u.h>
#include <libc.h>
#include <msg.h>

typedef struct {
    PyObject_HEAD
    Handle *h; // The Wily RPC handle
} WilyConnection;

static int
WilyConnection_init(WilyConnection *self, PyObject *args, PyObject *kwds) {
    int fd;
    fd = client_connect();
    if (fd < 0) {
        PyErr_SetString(PyExc_ConnectionError, "Could not open Wily RPC handle.");
        return -1;
    }

    // Initialize the Wily RPC handle using the file descriptor
    self->h = rpc_init(fd);
    if (!self->h) {
        PyErr_SetString(PyExc_ConnectionError, "Could not initialize Wily RPC handle.");
        return -1;
    }
    return 0;
}

static PyObject*
wily_list(WilyConnection *self, PyObject *Py_UNUSED(ignored)) {
    char *buf;
    char *err;

    if ((err = rpc_list(self->h, &buf))) {
        PyErr_SetString(PyExc_RuntimeError, err);
        return NULL;
    }

    PyObject *py_list = PyList_New(0);
    char *line = strtok(buf, "\n");
    while (line != NULL) {
        PyObject *item = PyUnicode_FromString(line);
        PyList_Append(py_list, item);
        Py_DECREF(item);
        line = strtok(NULL, "\n");
    }

    free(buf);
    return py_list;
}

static PyObject*
wily_new_window(WilyConnection *self, PyObject *args) {
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
        PyErr_SetString(PyExc_RuntimeError, err);
        return NULL;
    }

    // Return the new window ID as a Python integer
    return PyLong_FromLong((long)id);
}

static PyObject*
wily_event(WilyConnection *self, PyObject *args) {
    return NULL;
}

static PyObject*
wily_eventwouldblock(WilyConnection *self, PyObject *args) {
    return NULL;
}

static PyObject*
wily_bounce(WilyConnection *self, PyObject *args) {
    return NULL;
}

static PyObject*
wily_attach(WilyConnection *self, PyObject *args) {
    return NULL;
}

static PyObject*
wily_setname(WilyConnection *self, PyObject *args) {
    return NULL;
}

static PyObject*
wily_getname(WilyConnection *self, PyObject *args) {
    return NULL;
}

static PyObject*
wily_settools(WilyConnection *self, PyObject *args) {
    return NULL;
}

static PyObject*
wily_gettools(WilyConnection *self, PyObject *args) {
    return NULL;
}

static PyObject*
wily_read(WilyConnection *self, PyObject *args) {
    return NULL;
}

static PyObject*
wily_replace(WilyConnection *self, PyObject *args) {
    return NULL;
}

static PyObject*
wily_run(WilyConnection *self, PyObject *args) {
    return NULL;
}

static char con_goto__doc__[] = "goto(w:integer, from:long, to:long, s:string, flag:integer)has the same effect as sweeping 's' with B3  in window 'w',and starting any search at range(from,to), except that we onlywarp and select text if 'flag' is set.Returns a tuple (w:integer, from:long, to:long),which represents the window and selection that wasopened.";

static PyObject*
wily_goto(WilyConnection *self, PyObject *args) {
    char    *err;
    char    *name;
    Id      id;
    Range   r;
    long    from, to;
    int     flag;
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
    {"win", (PyCFunction)wily_new_window, METH_VARARGS, "Create or open a window by name."},
    {"list",   (PyCFunction)wily_list,       METH_NOARGS,  "List all open windows."},
    {"event",   (PyCFunction)wily_event,  1,  ""},
    {"eventwouldblock", (PyCFunction)wily_eventwouldblock,    1,  ""},
    {"bounce",  (PyCFunction)wily_bounce, 1,  ""},
    {"attach",  (PyCFunction)wily_attach, 1,  ""},
    {"setname", (PyCFunction)wily_setname,    1,  ""},
    {"getname", (PyCFunction)wily_getname,    1,  ""},
    {"settools",    (PyCFunction)wily_settools,   1,  ""},
    {"gettools",    (PyCFunction)wily_gettools,   1,  ""},
    {"read",    (PyCFunction)wily_read,   1,  ""},
    {"replace", (PyCFunction)wily_replace,    1,  ""},
    {"run", (PyCFunction)wily_run,    1,  ""},
    {"goto",    (PyCFunction)wily_goto,   1,  con_goto__doc__},
    {NULL, NULL, 0, NULL}
};

static PyTypeObject WilyConnectionType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "wily.Connection",
    .tp_doc = "Wily Editor Connection",
    .tp_basicsize = sizeof(WilyConnection),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_new = PyType_GenericNew,
    .tp_init = (initproc)WilyConnection_init,
    .tp_methods = WilyConnection_methods,
};

static struct PyModuleDef wilymodule = {
    PyModuleDef_HEAD_INIT,
    "wily",
    "Python interface for Wily RPC.",
    -1,
    NULL, NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC PyInit_wily(void) {
    PyObject *m;
    if (PyType_Ready(&WilyConnectionType) < 0)
        return NULL;

    m = PyModule_Create(&wilymodule);
    if (m == NULL)
        return NULL;

    Py_INCREF(&WilyConnectionType);
    if (PyModule_AddObject(m, "Connection", (PyObject *)&WilyConnectionType) < 0) {
        Py_DECREF(&WilyConnectionType);
        Py_DECREF(m);
        return NULL;
    }

    return m;
}
// vim: ts=4 sw=4 ft=c expandtab :
