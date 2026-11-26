#lang racket

(require data/queue
         ffi/unsafe
         racket/unix-socket)

(provide
 ;; Exceptions & structs
 (struct-out exn:fail:wily)
 (struct-out msg)
 make-msg
 wily-client?
 ;; Message types & masks
 WEexec WEgoto WEdestroy WEreplace WEfencepost
 WRerror
 WMlist WRlist
 WMnew WRnew
 WMattach WRattach
 WMsetname WRsetname
 WMgetname WRgetname
 WMsettools WRsettools
 WMgettools WRgettools
 WMread WRread
 WMreplace WRreplace
 WMexec WRexec
 WMgoto WRgoto
 WMfencepost
 ;; Connection & lifecycle
 get-wily-fifo
 make-wily-client
 wily-connect!
 wily-close!
 call-with-wily
 ;; RPC methods
 wily-list-windows
 wily-new
 wily-attach
 wily-setname
 wily-getname
 wily-settools
 wily-gettools
 wily-read
 wily-replace
 wily-exec-cmd
 wily-goto
 ;; Events
 wily-would-block?
 wily-next-event
 wily-bounce)

;; --- Exceptions ---
(struct exn:fail:wily exn:fail () #:transparent)

(define (raise-wily-error text)
  (raise (exn:fail:wily (format "Wily error: ~a" text)
                        (current-continuation-marks))))

;; --- Message Types ---
(define WEexec 1)
(define WEgoto 2)
(define WEdestroy 4)
(define WEreplace 8)
(define WEfencepost 9)

(define WRerror 10)
(define WMlist 11)
(define WRlist 12)
(define WMnew 13)
(define WRnew 14)
(define WMattach 15)
(define WRattach 16)
(define WMsetname 17)
(define WRsetname 18)
(define WMgetname 19)
(define WRgetname 20)
(define WMsettools 21)
(define WRsettools 22)
(define WMgettools 23)
(define WRgettools 24)
(define WMread 25)
(define WRread 26)
(define WMreplace 27)
(define WRreplace 28)
(define WMexec 29)
(define WRexec 30)
(define WMgoto 31)
(define WRgoto 32)
(define WMfencepost 33)

(define COOKIE #xFEED)
(define HEADERSIZE 22)

;; --- Message Struct & Binary Packing ---
(struct msg (t m w p0 p1 flag s) #:transparent)

(define (make-msg t #:m [m 0] #:w [w 0] #:p0 [p0 0] #:p1 [p1 0] #:flag [flag 0] #:s [s ""])
  (msg t m w p0 p1 flag s))

(define (pack-msg m)
  (define payload (bytes-append (string->bytes/utf-8 (msg-s m)) #"\0"))
  (define total-len (+ HEADERSIZE (bytes-length payload)))
  (bytes-append
   (integer->integer-bytes COOKIE 2 #f #t)
   (integer->integer-bytes (msg-t m) 2 #f #t)
   (integer->integer-bytes total-len 4 #f #t)
   (integer->integer-bytes (msg-m m) 2 #f #t)
   (integer->integer-bytes (msg-w m) 2 #f #t)
   (integer->integer-bytes (msg-p0 m) 4 #f #t)
   (integer->integer-bytes (msg-p1 m) 4 #f #t)
   (integer->integer-bytes (msg-flag m) 2 #f #t)
   payload))

;; --- Environment / FIFO Discovery ---
(define (fallback-username)
  (with-handlers ([exn:fail? (λ (_) #f)])
    (define getuid (get-ffi-obj "getuid" #f (_fun -> _uint32)))
    (define getpwuid (get-ffi-obj "getpwuid" #f (_fun _uint32 -> (_cpointer/null 'passwd))))
    (define pw (getpwuid (getuid)))
    (and pw (ptr-ref pw _string 0))))

(define (current-username)
  (or (getenv "USER")
      (getenv "LOGNAME")
      (fallback-username)
      (error 'get-wily-fifo "Could not determine username")))

(define (get-wily-fifo)
  (or (getenv "WILYFIFO")
      (let ([disp (getenv "DISPLAY")])
        (unless disp
          (error 'get-wily-fifo "$DISPLAY not set and $WILYFIFO not set"))
        (define tmpdir (or (getenv "TMPDIR") "/tmp"))
        (format "~a/wily~a~a" tmpdir (current-username) disp))))

;; --- Wily Client (Synchronizable Event via prop:evt) ---
(define (client->evt client)
  (cond
    [(non-empty-queue? (wily-client-events client))
     (wrap-evt always-evt (λ (_) (dequeue! (wily-client-events client))))]
    [(wily-client-in client)
     => (λ (in)
          (wrap-evt in (λ (_) (wily-next-event client))))]
    [else
     (error 'wily-client "Not connected to Wily")]))

(struct wily-client (fifo-path
                     [in #:mutable]
                     [out #:mutable]
                     [next-id #:mutable]
                     events)
  #:property prop:evt client->evt)

(define (make-wily-client [fifo-path (get-wily-fifo)])
  (wily-client fifo-path #f #f 1 (make-queue)))

(define (wily-connect! client)
  (define sock-path (make-temporary-file "wily.~a" #f "/tmp"))
  (delete-file sock-path)
  (define path-str (path->string sock-path))

  (define listener (unix-socket-listen path-str 1))
  (dynamic-wind
   void
   (λ ()
     (define path-bytes (string->bytes/utf-8 path-str))
     (call-with-output-file (wily-client-fifo-path client)
                            #:exists 'append
                            (λ (fifo-out)
                              (write-bytes path-bytes fifo-out)
                              (flush-output fifo-out)))
     (define-values (in out) (unix-socket-accept listener))
     (set-wily-client-in! client in)
     (set-wily-client-out! client out)
     client)
   (λ ()
     (unix-socket-close-listener listener)
     (when (file-exists? sock-path)
       (delete-file sock-path)))))

(define (wily-close! client)
  (when (wily-client-in client)
    (close-input-port (wily-client-in client))
    (set-wily-client-in! client #f))
  (when (wily-client-out client)
    (close-output-port (wily-client-out client))
    (set-wily-client-out! client #f)))

(define (call-with-wily proc [fifo-path (get-wily-fifo)])
  (define client (make-wily-client fifo-path))
  (dynamic-wind
   (λ () (wily-connect! client))
   (λ () (proc client))
   (λ () (wily-close! client))))

;; --- Low-Level Protocol Helpers ---
(define (alloc-id! client)
  (define mid (wily-client-next-id client))
  (set-wily-client-next-id! client (+ (modulo mid #xFFFF) 1))
  mid)

(define (recv-exact in n)
  (define buf (read-bytes n in))
  (when (or (eof-object? buf) (< (bytes-length buf) n))
    (error 'recv-exact "Wily closed the IPC connection"))
  buf)

(define (recv-raw-msg client)
  (define in (wily-client-in client))
  (unless in (error 'recv-raw-msg "Not connected to Wily"))
  (define hdr (recv-exact in HEADERSIZE))
  (define cookie (integer-bytes->integer hdr #f #t 0 2))
  (define mtype  (integer-bytes->integer hdr #f #t 2 4))
  (define tlen   (integer-bytes->integer hdr #f #t 4 8))
  (define mid    (integer-bytes->integer hdr #f #t 8 10))
  (define wid    (integer-bytes->integer hdr #f #t 10 12))
  (define p0     (integer-bytes->integer hdr #f #t 12 16))
  (define p1     (integer-bytes->integer hdr #f #t 16 20))
  (define flag   (integer-bytes->integer hdr #f #t 20 22))

  (when (or (not (= cookie COOKIE)) (< tlen HEADERSIZE))
    (error 'recv-raw-msg "Corrupt Wily packet: cookie=#x~x len=~a" cookie tlen))

  (define raw-text (recv-exact in (- tlen HEADERSIZE)))
  (define text-bytes (subbytes raw-text 0 (max 0 (sub1 (bytes-length raw-text)))))
  (define text (bytes->string/utf-8 text-bytes #\uFFFD))
  (msg mtype mid wid p0 p1 flag text))

(define (wily-rpc client req-type expected-reply
                  #:w [win-id 0] #:p0 [p0 0] #:p1 [p1 0] #:flag [flag 0] #:s [text ""])
  (define out (wily-client-out client))
  (unless out (error 'wily-rpc "Not connected to Wily"))

  (define mid (alloc-id! client))
  (define req (msg req-type mid win-id p0 p1 flag text))
  (write-bytes (pack-msg req) out)
  (flush-output out)

  (let loop ()
    (define m (recv-raw-msg client))
    (cond
      [(< (msg-t m) WEfencepost)
       (enqueue! (wily-client-events client) m)
       (loop)]
      [(not (= (msg-m m) mid))
       (error 'wily-rpc "Unexpected message id ~a (expected ~a)" (msg-m m) mid)]
      [(= (msg-t m) WRerror)
       (raise-wily-error (msg-s m))]
      [(not (= (msg-t m) expected-reply))
       (error 'wily-rpc "Unexpected reply type ~a (expected ~a)" (msg-t m) expected-reply)]
      [else m])))

;; --- High-Level RPC Methods ---
(define (wily-list-windows client)
  (define reply (wily-rpc client WMlist WRlist))
  (for/list ([line (in-list (string-split (msg-s reply) "\n"))]
             #:when (non-empty-string? (string-trim line)))
    (match (regexp-match #px"^(.+?)\\s+(\\d+)$" (string-trim line))
      [(list _ name wid-str) (cons (string->number wid-str) name)]
      [_ (error 'wily-list-windows "Malformed window entry: ~s" line)])))

(define (wily-new client name #:backup [backup #f])
  (define reply (wily-rpc client WMnew WRnew #:flag (if backup 1 0) #:s name))
  (msg-w reply))

(define (wily-attach client win-id mask)
  (void (wily-rpc client WMattach WRattach #:w win-id #:flag mask)))

(define (wily-setname client win-id name)
  (void (wily-rpc client WMsetname WRsetname #:w win-id #:s name)))

(define (wily-getname client win-id)
  (msg-s (wily-rpc client WMgetname WRgetname #:w win-id)))

(define (wily-settools client win-id tools)
  (void (wily-rpc client WMsettools WRsettools #:w win-id #:s tools)))

(define (wily-gettools client win-id)
  (msg-s (wily-rpc client WMgettools WRgettools #:w win-id)))

(define (wily-read client win-id p0 p1)
  (msg-s (wily-rpc client WMread WRread #:w win-id #:p0 p0 #:p1 p1)))

(define (wily-replace client win-id p0 p1 text)
  (void (wily-rpc client WMreplace WRreplace #:w win-id #:p0 p0 #:p1 p1 #:s text)))

(define (wily-exec-cmd client win-id cmd)
  (void (wily-rpc client WMexec WRexec #:w win-id #:s cmd)))

(define (wily-goto client win-id target #:set-dot [set-dot #t])
  (define reply (wily-rpc client WMgoto WRgoto
                          #:w win-id
                          #:flag (if set-dot 1 0)
                          #:s target))
  (values (msg-w reply) (msg-p0 reply) (msg-p1 reply)))

;; --- Event Queue & Bouncing ---
(define (wily-would-block? client)
  (cond
    [(non-empty-queue? (wily-client-events client)) #f]
    [(not (wily-client-in client)) #t]
    [else (not (byte-ready? (wily-client-in client)))]))

(define (wily-next-event client)
  (define q (wily-client-events client))
  (cond
    [(non-empty-queue? q) (dequeue! q)]
    [else
     (let loop ()
       (define m (recv-raw-msg client))
       (if (< (msg-t m) WEfencepost)
           m
           (loop)))]))

(define (wily-bounce client ev)
  (cond
    [(= (msg-t ev) WEexec)
     (void (wily-rpc client WMexec WRexec
                     #:w (msg-w ev) #:p0 (msg-p0 ev) #:p1 (msg-p1 ev)
                     #:flag (msg-flag ev) #:s (msg-s ev)))]
    [(= (msg-t ev) WEgoto)
     (void (wily-rpc client WMgoto WRgoto
                     #:w (msg-w ev) #:p0 (msg-p0 ev) #:p1 (msg-p1 ev)
                     #:flag (msg-flag ev) #:s (msg-s ev)))]))
;; --- Example Main ---
(module+ main
  (call-with-wily
   (λ (wily)
     (printf "Existing windows: ~a\n" (wily-list-windows wily))
     (define wid (wily-new wily "+RacketConsole" #:backup #f))
     (wily-settools wily wid " Hello Quit ")
     (wily-replace wily wid 0 0 "Middle-click (B2) 'Hello' or 'Quit' in the tag.\n")
     (wily-attach wily wid (bitwise-ior WEexec WEgoto WEdestroy))
     (let loop ()
       ;; Works with both (wily-next-event wily) and Racket's (sync wily ...)
       (define ev (sync wily))
       (cond
         [(= (msg-t ev) WEdestroy)
          (printf "Window closed, exiting.\n")]
         [(= (msg-t ev) WEexec)
          (define cmd (string-trim (msg-s ev)))
          (cond
            [(string=? cmd "Hello")
             (wily-replace wily wid 0 0 "Clicked Hello!\n")
             (loop)]
            [(string=? cmd "Quit")
             (wily-exec-cmd wily wid "Del")]
            [else
             (wily-bounce wily ev)
             (loop)])]
         [(= (msg-t ev) WEgoto)
          (wily-bounce wily ev)
          (loop)]
         [else (loop)])))))
