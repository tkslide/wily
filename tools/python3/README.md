# Wily Python3 Interface
## Installation:

```
python3 setup.py build
sudo python3 setup.py install
```


## Python doc:

```
NAME
    wily - Python interface for Wily RPC.

CLASSES
    builtins.Exception(builtins.BaseException)
        error
    builtins.object
        Connection

    class Connection(builtins.object)
     |  Wily Editor Connection
     |
     |  Methods defined here:
     |
     |  __init__(self, /, *args, **kwargs)
     |      Initialize self.  See help(type(self)) for accurate signature.
     |
     |  attach(...)
     |      attach(w:integer, mask:integer) 'w' is a window identifier as obtained by new() or list().  'mask' is a bitmask of event types. Sets the mask of events to be sent to us.
     |
     |  bounce(...)
     |      bounce(tuple) Called with an event tuple as returned by event().  Returns None
     |
     |  event(...)
     |      event() returns an event tuple (w, t, r0, r1, s), whose fields are: w: window identifier t: event type r0, r1: affected range s: string The meaning (if any) of the values of r0, r1 and s depend on the event type 't'
     |
     |  eventwouldblock(...)
     |      eventwouldblock() If eventwouldblock() returns true, calling event() might have to wait.  If eventwouldblock() returns false, calling event() would return immediately because an event is already queued up and waiting
     |
     |  getname(...)
     |      getname(w:integer) : string Return the name currently visible in w's tag
     |
     |  gettools(...)
     |      gettools(w:integer) : string Return the tools currently visible in w's tag
     |
     |  goto(...)
     |      goto(w:integer, from:long, to:long, s:string, flag:integer)has the same effect as sweeping 's' with B3  in window 'w',and starting any search at range(from,to), except that we onlywarp and select text if 'flag' is set.Returns a tuple (w:integer, from:long, to:long),which represents the window and selection that wasopened.
     |
     |  list(self, /)
     |      List currently open windows list() Returns list of (name, id) tuples representing currently open windows
     |
     |  read(...)
     |      read(w:integer, from:integer, to:integer) returns a (UTF) string
     |
     |  replace(...)
     |      replace(w:integer, from:integer, to:integer, s:string) replace the text in 'w' from 'from' to 'to' with 's'
     |
     |  run(...)
     |      run(w:integer, s:string) has the same effect as sweeping 's' with B2  in window 'w'
     |
     |  setname(...)
     |      setname(w:integer, s:string) Set w's name to 's'
     |
     |  settools(...)
     |      settools(w:integer, s:string) Set w's tools to 's'
     |
     |  win(...)
     |      open a window win(name:string, isBackedUp:integer) Returns an integer window identifier, to be used for later operations on the window
     |
     |  ----------------------------------------------------------------------
     |  Static methods defined here:
     |
     |  __new__(*args, **kwargs)
     |      Create and return a new object.  See help(type) for accurate signature.

    class error(builtins.Exception)
     |  Method resolution order:
     |      error
     |      builtins.Exception
     |      builtins.BaseException
     |      builtins.object
     |
     |  Data descriptors defined here:
     |
     |  __weakref__
     |      list of weak references to the object
     |
     |  ----------------------------------------------------------------------
     |  Methods inherited from builtins.Exception:
     |
     |  __init__(self, /, *args, **kwargs)
     |      Initialize self.  See help(type(self)) for accurate signature.
     |
     |  ----------------------------------------------------------------------
     |  Static methods inherited from builtins.Exception:
     |
     |  __new__(*args, **kwargs) class method of builtins.Exception
     |      Create and return a new object.  See help(type) for accurate signature.
     |
     |  ----------------------------------------------------------------------
     |  Methods inherited from builtins.BaseException:
     |
     |  __getattribute__(self, name, /)
     |      Return getattr(self, name).
     |
     |  __reduce__(self, /)
     |      Helper for pickle.
     |
     |  __repr__(self, /)
     |      Return repr(self).
     |
     |  __setstate__(self, object, /)
     |
     |  __str__(self, /)
     |      Return str(self).
     |
     |  add_note(self, object, /)
     |      Exception.add_note(note) --
     |      add a note to the exception
     |
     |  with_traceback(self, object, /)
     |      Exception.with_traceback(tb) --
     |      set self.__traceback__ to tb and return self.
     |
     |  ----------------------------------------------------------------------
     |  Data descriptors inherited from builtins.BaseException:
     |
     |  __cause__
     |      exception cause
     |
     |  __context__
     |      exception context
     |
     |  __dict__
     |
     |  __suppress_context__
     |
     |  __traceback__
     |
     |  args

DATA
    DESTROY = 4
    EXEC = 1
    GOTO = 2
    REPLACE = 8

FILE
    /usr/local/lib/python3.13/dist-packages/wily-1.0-py3.13-linux-x86_64.egg/wily.cpython-313-x86_64-linux-gnu.so
```
