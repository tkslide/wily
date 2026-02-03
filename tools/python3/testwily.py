#!/usr/bin/python3 -u
# -u == unbuffered output

from time import sleep
import wily
print('get connection')
c = wily.Connection()
sleep(1)

print('get window "fish"')
w = c.win('fish', 1)
print('new window', w)
sleep(1)

print('get list of windows')
print('listing', c.list())
sleep(1)

print('change name from "fish" to "newname"')
c.setname(w, 'newname')
sleep(1)

print('get tools')
tools = c.gettools(w);
sleep(1)

print('set tools')
c.settools(w, tools.upper())
sleep(1)

print('insert some text')
c.replace(w, 0, 0, 'pack my box with five dozen liquor jugs')
sleep(1)

print('search for the text "box"')
w2,f,t  =  c.goto(w, 0, 0, 'box', 0)
print('found at', w2, f, t)
sleep(1)

print('replace "box" with "crate"')
c.replace(w, f, t, 'crate')
sleep(1)

print('get length of the current window')
print(c.length(w))
sleep(1)

print('get current content')
print(c.read(w,0,t))
sleep(1)

print('search for the whole window')
w2,f,t  =  c.goto(w, 0, 0, ':,', 0)
print('whole file is', w2, f, t)
sleep(1)

print('replace the whole window with another phrase')
c.replace(w, f, t, 'the quick brown fox jumped over the lazy dog')
sleep(1)

print('read the range [10,15) from the window')
s = c.read(w, 10, 15)
print('read ', s)

sleep(1)

print('attach to the window, grab EXEC messages only')
c.attach(w, wily.EXEC)
sleep(1)

num_events = 3
print(f'print the next {num_events} events and bounce them')
for j in range(num_events):
  print('waiting')
  wily_evt = c.event()
  print('got', wily_evt)
  c.bounce(wily_evt)

print('run')
c.run(w,'Split')
sleep(1)


print('done')


# vim: ts=2 sw=2 expandtab :
