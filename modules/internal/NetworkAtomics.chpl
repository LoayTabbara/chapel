pragma "no use ChapelStandard"
module NetworkAtomics {
  const LINENO = -1:int(32); // it'd be nice if we had something like __LINENO__

  // int(64)
  extern proc chpl_comm_atomic_get_int64(ref result:int(64),
                                         l:int(32), ref obj:int(64),
                                         ln:int(32), fn:string);
  extern proc chpl_comm_atomic_put_int64(ref desired:int(64),
                                         l:int(32), ref obj:int(64),
                                         ln:int(32), fn:string);
  extern proc chpl_comm_atomic_add_int64(ref op:int(64),
                                         l:int(32), ref obj:int(64),
                                         ln:int(32), fn:string);
  extern proc chpl_comm_atomic_fetch_add_int64(ref op:int(64),
                                               l:int(32), ref obj:int(64),
                                               ref result:int(64),
                                               ln:int(32), fn:string);
  extern proc chpl_comm_atomic_sub_int64(ref op:int(64),
                                         l:int(32), ref obj:int(64),
                                         ln:int(32), fn:string);
  extern proc chpl_comm_atomic_fetch_sub_int64(ref op:int(64),
                                               l:int(32), ref obj:int(64),
                                               ref result:int(64),
                                               ln:int(32), fn:string);
  extern proc chpl_comm_atomic_and_int64(ref op:int(64),
                                         l:int(32), ref obj:int(64),
                                         ln:int(32), fn:string);
  extern proc chpl_comm_atomic_fetch_and_int64(ref op:int(64),
                                               l:int(32), ref obj:int(64),
                                               ref result:int(64),
                                               ln:int(32), fn:string);
  extern proc chpl_comm_atomic_or_int64(ref op:int(64),
                                        l:int(32), ref obj:int(64),
                                        ln:int(32), fn:string);
  extern proc chpl_comm_atomic_fetch_or_int64(ref op:int(64),
                                              l:int(32), ref obj:int(64),
                                              ref result:int(64),
                                              ln:int(32), fn:string);
  extern proc chpl_comm_atomic_xor_int64(ref op:int(64),
                                         l:int(32), ref obj:int(64),
                                         ln:int(32), fn:string);
  extern proc chpl_comm_atomic_fetch_xor_int64(ref op:int(64),
                                               l:int(32), ref obj:int(64),
                                               ref result:int(64),
                                               ln:int(32), fn:string);
  extern proc chpl_comm_atomic_xchg_int64(ref desired:int(64),
                                          l:int(32), ref obj:int(64),
                                          ref result:int(64),
                                          ln:int(32), fn:string);
  extern proc chpl_comm_atomic_cmpxchg_int64(ref expected:int(64),
                                             ref desired:int(64),
                                             l:int(32), ref obj:int(64),
                                             ref result:bool(32),
                                             ln:int(32), fn:string);

  // int(64)
  pragma "atomic type"
  record ratomic_int64 {
    var _v: int(64);
    inline proc read() {
      var ret: int(64);
      chpl_comm_atomic_get_int64(ret, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc write(value:int(64)) {
      var v = value;
      chpl_comm_atomic_put_int64(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc exchange(value:int(64)):int(64) {
      var ret:int(64);
      var v = value;
      chpl_comm_atomic_xchg_int64(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc compareExchange(expected:int(64), desired:int(64)):bool {
      var ret:bool(32);
      var te = expected;
      var td = desired;
      chpl_comm_atomic_cmpxchg_int64(te, td, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc compareExchangeWeak(expected:int(64), desired:int(64)):bool {
      return this.compareExchange(expected, desired);
    }
    inline proc compareExchangeStrong(expected:int(64), desired:int(64)):bool {
      return this.compareExchange(expected, desired);
    }

    inline proc fetchAdd(value:int(64)):int(64) {
      var v = value;
      var ret:int(64);
      chpl_comm_atomic_fetch_add_int64(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc add(value:int(64)):int(64) {
      var v = value;
      chpl_comm_atomic_add_int64(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc fetchSub(value:int(64)):int(64) {
      var v = value;
      var ret:int(64);
      chpl_comm_atomic_fetch_sub_int64(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc sub(value:int(64)):int(64) {
      var v = value;
      chpl_comm_atomic_sub_int64(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc fetchOr(value:int(64)):int(64) {
      var v = value;
      var ret:int(64);
      chpl_comm_atomic_fetch_or_int64(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc or(value:int(64)):int(64) {
      var v = value;
      chpl_comm_atomic_or_int64(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc fetchAnd(value:int(64)):int(64) {
      var v = value;
      var ret:int(64);
      chpl_comm_atomic_fetch_and_int64(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc and(value:int(64)):int(64) {
      var v = value;
      chpl_comm_atomic_and_int64(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc fetchXor(value:int(64)):int(64) {
      var v = value;
      var ret:int(64);
      chpl_comm_atomic_fetch_xor_int64(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc xor(value:int(64)):int(64) {
      var v = value;
      chpl_comm_atomic_xor_int64(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc waitFor(val:int(64)) {
      on this do while (read() != val) do chpl_task_yield();
    }

    inline proc peek() {
      return _v;
    }
    inline proc poke(value:int(64)) {
      _v = value;
    }

    proc writeThis(x: Writer) {
      x.write(read());
    }
  }

  inline proc =(ref a:ratomic_int64, b:ratomic_int64) {
    a.write(b.read());
  }
  inline proc =(ref a:ratomic_int64, b) {
    compilerError("Cannot directly assign network atomic variables");
  }
  inline proc +(a:ratomic_int64, b) {
    compilerError("Cannot directly add network atomic variables");
    return a;
  }
  inline proc -(a:ratomic_int64, b) {
    compilerError("Cannot directly subtract network atomic variables");
    return a;
  }
  inline proc *(a:ratomic_int64, b) {
    compilerError("Cannot directly multiply network atomic variables");
    return a;
  }
  inline proc /(a:ratomic_int64, b) {
    compilerError("Cannot directly divide network atomic variables");
    return a;
  }
  inline proc %(a:ratomic_int64, b) {
    compilerError("Cannot directly divide network atomic variables");
    return a;
  }


  // int(32)
  extern proc chpl_comm_atomic_get_int32(ref result:int(32),
                                         l:int(32), ref obj:int(32),
                                         ln:int(32), fn:string);
  extern proc chpl_comm_atomic_put_int32(ref desired:int(32),
                                         l:int(32), ref obj:int(32),
                                         ln:int(32), fn:string);
  extern proc chpl_comm_atomic_add_int32(ref op:int(32),
                                         l:int(32), ref obj:int(32),
                                         ln:int(32), fn:string);
  extern proc chpl_comm_atomic_fetch_add_int32(ref op:int(32),
                                               l:int(32), ref obj:int(32),
                                               ref result:int(32),
                                               ln:int(32), fn:string);
  extern proc chpl_comm_atomic_sub_int32(ref op:int(32),
                                         l:int(32), ref obj:int(32),
                                         ln:int(32), fn:string);
  extern proc chpl_comm_atomic_fetch_sub_int32(ref op:int(32),
                                               l:int(32), ref obj:int(32),
                                               ref result:int(32),
                                               ln:int(32), fn:string);
  extern proc chpl_comm_atomic_and_int32(ref op:int(32),
                                         l:int(32), ref obj:int(32),
                                         ln:int(32), fn:string);
  extern proc chpl_comm_atomic_fetch_and_int32(ref op:int(32),
                                               l:int(32), ref obj:int(32),
                                               ref result:int(32),
                                               ln:int(32), fn:string);
  extern proc chpl_comm_atomic_or_int32(ref op:int(32),
                                        l:int(32), ref obj:int(32),
                                        ln:int(32), fn:string);
  extern proc chpl_comm_atomic_fetch_or_int32(ref op:int(32),
                                              l:int(32), ref obj:int(32),
                                              ref result:int(32),
                                              ln:int(32), fn:string);
  extern proc chpl_comm_atomic_xor_int32(ref op:int(32),
                                         l:int(32), ref obj:int(32),
                                         ln:int(32), fn:string);
  extern proc chpl_comm_atomic_fetch_xor_int32(ref op:int(32),
                                               l:int(32), ref obj:int(32),
                                               ref result:int(32),
                                               ln:int(32), fn:string);
  extern proc chpl_comm_atomic_xchg_int32(ref desired:int(32),
                                          l:int(32), ref obj:int(32),
                                          ref result:int(32),
                                          ln:int(32), fn:string);
  extern proc chpl_comm_atomic_cmpxchg_int32(ref expected:int(32),
                                             ref desired:int(32),
                                             l:int(32), ref obj:int(32),
                                             ref result:bool(32),
                                             ln:int(32), fn:string);

  // int32
  pragma "atomic type"
  record ratomic_int32 {
    var _v: int(32);
    inline proc read() {
      var ret: int(32);
      chpl_comm_atomic_get_int32(ret, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc write(value:int(32)) {
      var v = value;
      chpl_comm_atomic_put_int32(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc exchange(value:int(32)):int(32) {
      var ret:int(32);
      var v = value;
      chpl_comm_atomic_xchg_int32(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc compareExchange(expected:int(32), desired:int(32)):bool {
      var ret:bool(32);
      var te = expected;
      var td = desired;
      chpl_comm_atomic_cmpxchg_int32(te, td, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc compareExchangeWeak(expected:int(32), desired:int(32)):bool {
      return this.compareExchange(expected, desired);
    }
    inline proc compareExchangeStrong(expected:int(32), desired:int(32)):bool {
      return this.compareExchange(expected, desired);
    }

    inline proc fetchAdd(value:int(32)):int(32) {
      var v = value;
      var ret:int(32);
      chpl_comm_atomic_fetch_add_int32(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc add(value:int(32)):int(32) {
      var v = value;
      chpl_comm_atomic_add_int32(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc fetchSub(value:int(32)):int(32) {
      var v = value;
      var ret:int(32);
      chpl_comm_atomic_fetch_sub_int32(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc sub(value:int(32)):int(32) {
      var v = value;
      chpl_comm_atomic_sub_int32(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc fetchOr(value:int(32)):int(32) {
      var v = value;
      var ret:int(32);
      chpl_comm_atomic_fetch_or_int32(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc or(value:int(32)):int(32) {
      var v = value;
      chpl_comm_atomic_or_int32(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc fetchAnd(value:int(32)):int(32) {
      var v = value;
      var ret:int(32);
      chpl_comm_atomic_fetch_and_int32(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc and(value:int(32)):int(32) {
      var v = value;
      chpl_comm_atomic_and_int32(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc fetchXor(value:int(32)):int(32) {
      var v = value;
      var ret:int(32);
      chpl_comm_atomic_fetch_xor_int32(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc xor(value:int(32)):int(32) {
      var v = value;
      chpl_comm_atomic_xor_int32(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc waitFor(val:int(32)) {
      on this do while (read() != val) do chpl_task_yield();
    }

    inline proc peek() {
      return _v;
    }
    inline proc poke(value:int(32)) {
      _v = value;
    }

    proc writeThis(x: Writer) {
      x.write(read());
    }
  }

  inline proc =(ref a:ratomic_int32, b:ratomic_int32) {
    a.write(b.read());
  }
  inline proc =(ref a:ratomic_int32, b) {
    compilerError("Cannot directly assign network atomic variables");
  }
  inline proc +(a:ratomic_int32, b) {
    compilerError("Cannot directly add network atomic variables");
    return a;
  }
  inline proc -(a:ratomic_int32, b) {
    compilerError("Cannot directly subtract network atomic variables");
    return a;
  }
  inline proc *(a:ratomic_int32, b) {
    compilerError("Cannot directly multiply network atomic variables");
    return a;
  }
  inline proc /(a:ratomic_int32, b) {
    compilerError("Cannot directly divide network atomic variables");
    return a;
  }
  inline proc %(a:ratomic_int32, b) {
    compilerError("Cannot directly divide network atomic variables");
    return a;
  }


  // uint(64)
  extern proc chpl_comm_atomic_get_uint64(ref result:uint(64),
                                         l:int(32), ref obj:uint(64),
                                         ln:int(32), fn:string);
  extern proc chpl_comm_atomic_put_uint64(ref desired:uint(64),
                                         l:int(32), ref obj:uint(64),
                                         ln:int(32), fn:string);
  extern proc chpl_comm_atomic_add_uint64(ref op:uint(64),
                                         l:int(32), ref obj:uint(64),
                                         ln:int(32), fn:string);
  extern proc chpl_comm_atomic_fetch_add_uint64(ref op:uint(64),
                                               l:int(32), ref obj:uint(64),
                                               ref result:uint(64),
                                               ln:int(32), fn:string);
  extern proc chpl_comm_atomic_sub_uint64(ref op:uint(64),
                                         l:int(32), ref obj:uint(64),
                                         ln:int(32), fn:string);
  extern proc chpl_comm_atomic_fetch_sub_uint64(ref op:uint(64),
                                               l:int(32), ref obj:uint(64),
                                               ref result:uint(64),
                                               ln:int(32), fn:string);
  extern proc chpl_comm_atomic_and_uint64(ref op:uint(64),
                                         l:int(32), ref obj:uint(64),
                                         ln:int(32), fn:string);
  extern proc chpl_comm_atomic_fetch_and_uint64(ref op:uint(64),
                                               l:int(32), ref obj:uint(64),
                                               ref result:uint(64),
                                               ln:int(32), fn:string);
  extern proc chpl_comm_atomic_or_uint64(ref op:uint(64),
                                        l:int(32), ref obj:uint(64),
                                        ln:int(32), fn:string);
  extern proc chpl_comm_atomic_fetch_or_uint64(ref op:uint(64),
                                              l:int(32), ref obj:uint(64),
                                              ref result:uint(64),
                                              ln:int(32), fn:string);
  extern proc chpl_comm_atomic_xor_uint64(ref op:uint(64),
                                         l:int(32), ref obj:uint(64),
                                         ln:int(32), fn:string);
  extern proc chpl_comm_atomic_fetch_xor_uint64(ref op:uint(64),
                                               l:int(32), ref obj:uint(64),
                                               ref result:uint(64),
                                               ln:int(32), fn:string);
  extern proc chpl_comm_atomic_xchg_uint64(ref desired:uint(64),
                                          l:int(32), ref obj:uint(64),
                                          ref result:uint(64),
                                          ln:int(32), fn:string);
  extern proc chpl_comm_atomic_cmpxchg_uint64(ref expected:uint(64),
                                             ref desired:uint(64),
                                             l:int(32), ref obj:uint(64),
                                             ref result:bool(32),
                                             ln:int(32), fn:string);

  // uint(64)
  pragma "atomic type"
  record ratomic_uint64 {
    var _v: uint(64);
    inline proc read() {
      var ret: uint(64);
      chpl_comm_atomic_get_uint64(ret, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc write(value:uint(64)) {
      var v = value;
      chpl_comm_atomic_put_uint64(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc exchange(value:uint(64)):uint(64) {
      var ret:uint(64);
      var v = value;
      chpl_comm_atomic_xchg_uint64(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc compareExchange(expected:uint(64), desired:uint(64)):bool {
      var ret:bool(32);
      var te = expected;
      var td = desired;
      chpl_comm_atomic_cmpxchg_uint64(te, td, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc compareExchangeWeak(expected:uint(64), desired:uint(64)):bool {
      return this.compareExchange(expected, desired);
    }
    inline proc compareExchangeStrong(expected:uint(64), desired:uint(64)):bool {
      return this.compareExchange(expected, desired);
    }

    inline proc fetchAdd(value:uint(64)):uint(64) {
      var v = value;
      var ret:uint(64);
      chpl_comm_atomic_fetch_add_uint64(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc add(value:uint(64)):uint(64) {
      var v = value;
      chpl_comm_atomic_add_uint64(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc fetchSub(value:uint(64)):uint(64) {
      var v = value;
      var ret:uint(64);
      chpl_comm_atomic_fetch_sub_uint64(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc sub(value:uint(64)):uint(64) {
      var v = value;
      chpl_comm_atomic_sub_uint64(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc fetchOr(value:uint(64)):uint(64) {
      var v = value;
      var ret:uint(64);
      chpl_comm_atomic_fetch_or_uint64(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc or(value:uint(64)):uint(64) {
      var v = value;
      chpl_comm_atomic_or_uint64(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc fetchAnd(value:uint(64)):uint(64) {
      var v = value;
      var ret:uint(64);
      chpl_comm_atomic_fetch_and_uint64(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc and(value:uint(64)):uint(64) {
      var v = value;
      chpl_comm_atomic_and_uint64(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc fetchXor(value:uint(64)):uint(64) {
      var v = value;
      var ret:uint(64);
      chpl_comm_atomic_fetch_xor_uint64(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc xor(value:uint(64)):uint(64) {
      var v = value;
      chpl_comm_atomic_xor_uint64(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc waitFor(val:uint(64)) {
      on this do while (read() != val) do chpl_task_yield();
    }

    inline proc peek() {
      return _v;
    }
    inline proc poke(value:uint(64)) {
      _v = value;
    }

    proc writeThis(x: Writer) {
      x.write(read());
    }
  }

  inline proc =(ref a:ratomic_uint64, b:ratomic_uint64) {
    a.write(b.read());
  }
  inline proc =(ref a:ratomic_uint64, b) {
    compilerError("Cannot directly assign network atomic variables");
  }
  inline proc +(a:ratomic_uint64, b) {
    compilerError("Cannot directly add network atomic variables");
    return a;
  }
  inline proc -(a:ratomic_uint64, b) {
    compilerError("Cannot directly subtract network atomic variables");
    return a;
  }
  inline proc *(a:ratomic_uint64, b) {
    compilerError("Cannot directly multiply network atomic variables");
    return a;
  }
  inline proc /(a:ratomic_uint64, b) {
    compilerError("Cannot directly divide network atomic variables");
    return a;
  }
  inline proc %(a:ratomic_uint64, b) {
    compilerError("Cannot directly divide network atomic variables");
    return a;
  }


  // uint(32)
  extern proc chpl_comm_atomic_get_uint32(ref result:uint(32),
                                         l:int(32), ref obj:uint(32),
                                         ln:int(32), fn:string);
  extern proc chpl_comm_atomic_put_uint32(ref desired:uint(32),
                                         l:int(32), ref obj:uint(32),
                                         ln:int(32), fn:string);
  extern proc chpl_comm_atomic_add_uint32(ref op:uint(32),
                                         l:int(32), ref obj:uint(32),
                                         ln:int(32), fn:string);
  extern proc chpl_comm_atomic_fetch_add_uint32(ref op:uint(32),
                                               l:int(32), ref obj:uint(32),
                                               ref result:uint(32),
                                               ln:int(32), fn:string);
  extern proc chpl_comm_atomic_sub_uint32(ref op:uint(32),
                                         l:int(32), ref obj:uint(32),
                                         ln:int(32), fn:string);
  extern proc chpl_comm_atomic_fetch_sub_uint32(ref op:uint(32),
                                               l:int(32), ref obj:uint(32),
                                               ref result:uint(32),
                                               ln:int(32), fn:string);
  extern proc chpl_comm_atomic_and_uint32(ref op:uint(32),
                                         l:int(32), ref obj:uint(32),
                                         ln:int(32), fn:string);
  extern proc chpl_comm_atomic_fetch_and_uint32(ref op:uint(32),
                                               l:int(32), ref obj:uint(32),
                                               ref result:uint(32),
                                               ln:int(32), fn:string);
  extern proc chpl_comm_atomic_or_uint32(ref op:uint(32),
                                        l:int(32), ref obj:uint(32),
                                        ln:int(32), fn:string);
  extern proc chpl_comm_atomic_fetch_or_uint32(ref op:uint(32),
                                              l:int(32), ref obj:uint(32),
                                              ref result:uint(32),
                                              ln:int(32), fn:string);
  extern proc chpl_comm_atomic_xor_uint32(ref op:uint(32),
                                         l:int(32), ref obj:uint(32),
                                         ln:int(32), fn:string);
  extern proc chpl_comm_atomic_fetch_xor_uint32(ref op:uint(32),
                                               l:int(32), ref obj:uint(32),
                                               ref result:uint(32),
                                               ln:int(32), fn:string);
  extern proc chpl_comm_atomic_xchg_uint32(ref desired:uint(32),
                                          l:int(32), ref obj:uint(32),
                                          ref result:uint(32),
                                          ln:int(32), fn:string);
  extern proc chpl_comm_atomic_cmpxchg_uint32(ref expected:uint(32),
                                             ref desired:uint(32),
                                             l:int(32), ref obj:uint(32),
                                             ref result:bool(32),
                                             ln:int(32), fn:string);

  // uint(32)
  pragma "atomic type"
  record ratomic_uint32 {
    var _v: uint(32);
    inline proc read() {
      var ret: uint(32);
      chpl_comm_atomic_get_uint32(ret, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc write(value:uint(32)) {
      var v = value;
      chpl_comm_atomic_put_uint32(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc exchange(value:uint(32)):uint(32) {
      var ret:uint(32);
      var v = value;
      chpl_comm_atomic_xchg_uint32(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc compareExchange(expected:uint(32), desired:uint(32)):bool {
      var ret:bool(32);
      var te = expected;
      var td = desired;
      chpl_comm_atomic_cmpxchg_uint32(te, td, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc compareExchangeWeak(expected:uint(32), desired:uint(32)):bool {
      return this.compareExchange(expected, desired);
    }
    inline proc compareExchangeStrong(expected:uint(32), desired:uint(32)):bool {
      return this.compareExchange(expected, desired);
    }

    inline proc fetchAdd(value:uint(32)):uint(32) {
      var v = value;
      var ret:uint(32);
      chpl_comm_atomic_fetch_add_uint32(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc add(value:uint(32)):uint(32) {
      var v = value;
      chpl_comm_atomic_add_uint32(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc fetchSub(value:uint(32)):uint(32) {
      var v = value;
      var ret:uint(32);
      chpl_comm_atomic_fetch_sub_uint32(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc sub(value:uint(32)):uint(32) {
      var v = value;
      chpl_comm_atomic_sub_uint32(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc fetchOr(value:uint(32)):uint(32) {
      var v = value;
      var ret:uint(32);
      chpl_comm_atomic_fetch_or_uint32(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc or(value:uint(32)):uint(32) {
      var v = value;
      chpl_comm_atomic_or_uint32(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc fetchAnd(value:uint(32)):uint(32) {
      var v = value;
      var ret:uint(32);
      chpl_comm_atomic_fetch_and_uint32(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc and(value:uint(32)):uint(32) {
      var v = value;
      chpl_comm_atomic_and_uint32(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc fetchXor(value:uint(32)):uint(32) {
      var v = value;
      var ret:uint(32);
      chpl_comm_atomic_fetch_xor_uint32(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc xor(value:uint(32)):uint(32) {
      var v = value;
      chpl_comm_atomic_xor_uint32(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc waitFor(val:uint(32)) {
      on this do while (read() != val) do chpl_task_yield();
    }

    inline proc peek() {
      return _v;
    }
    inline proc poke(value:uint(32)) {
      _v = value;
    }

    proc writeThis(x: Writer) {
      x.write(read());
    }
  }

  inline proc =(ref a:ratomic_uint32, b:ratomic_uint32) {
    a.write(b.read());
  }
  inline proc =(ref a:ratomic_uint32, b) {
    compilerError("Cannot directly assign network atomic variables");
  }
  inline proc +(a:ratomic_uint32, b) {
    compilerError("Cannot directly add network atomic variables");
    return a;
  }
  inline proc -(a:ratomic_uint32, b) {
    compilerError("Cannot directly subtract network atomic variables");
    return a;
  }
  inline proc *(a:ratomic_uint32, b) {
    compilerError("Cannot directly multiply network atomic variables");
    return a;
  }
  inline proc /(a:ratomic_uint32, b) {
    compilerError("Cannot directly divide network atomic variables");
    return a;
  }
  inline proc %(a:ratomic_uint32, b) {
    compilerError("Cannot directly divide network atomic variables");
    return a;
  }


  // bool, implemented with int(64)
  pragma "atomic type"
  record ratomicflag {
    var _v: int(64);
    inline proc read() {
      var ret: int(64);
      chpl_comm_atomic_get_int64(ret, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
      return ret:bool;
    }
    inline proc write(value:bool) {
      var v = value:int(64);
      chpl_comm_atomic_put_int64(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc exchange(value:bool):bool {
      var ret:int(64);
      var v = value:int(64);
      chpl_comm_atomic_xchg_int64(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret:bool;
    }
    inline proc compareExchange(expected:bool, desired:bool):bool {
      var ret:bool(32);
      var te = expected:int(64);
      var td = desired:int(64);
      chpl_comm_atomic_cmpxchg_int64(te, td, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc compareExchangeWeak(expected:bool, desired:bool):bool {
      return this.compareExchange(expected, desired);
    }
    inline proc compareExchangeStrong(expected:bool, desired:bool):bool {
      return this.compareExchange(expected, desired);
    }

    inline proc testAndSet() {
      return this.exchange(true);
    }
    inline proc clear() {
      this.write(false);
    }

    inline proc waitFor(val:bool) {
      on this do while (read() != val) do chpl_task_yield();
    }

    inline proc peek() {
      return _v;
    }
    inline proc poke(value:int(64)) {
      _v = value;
    }

    proc writeThis(x: Writer) {
      x.write(read());
    }
  }

  inline proc =(ref a:ratomicflag, b:ratomicflag) {
    a.write(b.read());
  }
  inline proc =(ref a:ratomicflag, b) {
    compilerError("Cannot directly assign network atomic variables");
  }
  inline proc +(a:ratomicflag, b) {
    compilerError("Cannot directly add network atomic variables");
    return a;
  }
  inline proc -(a:ratomicflag, b) {
    compilerError("Cannot directly subtract network atomic variables");
    return a;
  }
  inline proc *(a:ratomicflag, b) {
    compilerError("Cannot directly multiply network atomic variables");
    return a;
  }
  inline proc /(a:ratomicflag, b) {
    compilerError("Cannot directly divide network atomic variables");
    return a;
  }
  inline proc %(a:ratomicflag, b) {
    compilerError("Cannot directly divide network atomic variables");
    return a;
  }


  // real(64)
  extern proc chpl_comm_atomic_get_real64(ref result:real(64),
                                          l:int(32), ref obj:real(64),
                                          ln:int(32), fn:string);
  extern proc chpl_comm_atomic_put_real64(ref desired:real(64),
                                          l:int(32), ref obj:real(64),
                                          ln:int(32), fn:string);
  extern proc chpl_comm_atomic_add_real64(ref op:real(64),
                                          l:int(32), ref obj:real(64),
                                          ln:int(32), fn:string);
  extern proc chpl_comm_atomic_fetch_add_real64(ref op:real(64),
                                                l:int(32), ref obj:real(64),
                                                ref result:real(64),
                                                ln:int(32), fn:string);
  extern proc chpl_comm_atomic_sub_real64(ref op:real(64),
                                          l:int(32), ref obj:real(64),
                                          ln:int(32), fn:string);
  extern proc chpl_comm_atomic_fetch_sub_real64(ref op:real(64),
                                                l:int(32), ref obj:real(64),
                                                ref result:real(64),
                                                ln:int(32), fn:string);
  extern proc chpl_comm_atomic_xchg_real64(ref desired:real(64),
                                           l:int(32), ref obj:real(64),
                                           ref result:real(64),
                                           ln:int(32), fn:string);
  extern proc chpl_comm_atomic_cmpxchg_real64(ref expected:real(64),
                                              ref desired:real(64),
                                              l:int(32), ref obj:real(64),
                                              ref result:bool(32),
                                              ln:int(32), fn:string);
  
  pragma "atomic type"
  record ratomic_real64 {
    var _v: real(64);
    inline proc read() {
      var ret: real(64);
      chpl_comm_atomic_get_real64(ret, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc write(value:real(64)) {
      var v = value;
      chpl_comm_atomic_put_real64(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc exchange(value:real(64)):real(64) {
      var ret:real(64);
      var v = value;
      chpl_comm_atomic_xchg_real64(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc compareExchange(expected:real(64), desired:real(64)):bool {
      var ret:bool(32);
      var te = expected;
      var td = desired;
      chpl_comm_atomic_cmpxchg_real64(te, td, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc compareExchangeWeak(expected:real(64), desired:real(64)):bool {
      return compareExchange(expected, desired);
    }
    inline proc compareExchangeStrong(expected:real(64), desired:real(64)):bool {
      return compareExchange(expected, desired);
    }

    inline proc fetchAdd(value:real(64)):real(64) {
      var v = value;
      var ret:real(64);
      chpl_comm_atomic_fetch_add_real64(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc add(value:real(64)):real(64) {
      var v = value;
      chpl_comm_atomic_add_real64(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc fetchSub(value:real(64)):real(64) {
      var v = value;
      var ret:real(64);
      chpl_comm_atomic_fetch_sub_real64(v, this.locale.id:int(32), this._v, ret, LINENO, "NetworkAtomics.chpl");
      return ret;
    }
    inline proc sub(value:real(64)):real(64) {
      var v = value;
      chpl_comm_atomic_sub_real64(v, this.locale.id:int(32), this._v, LINENO, "NetworkAtomics.chpl");
    }

    inline proc fetchOr(value:real(64)):real(64) {
      compilerError("or not definted for network atomic real");
    }
    inline proc or(value:real(64)):real(64) {
      compilerError("or not definted for network atomic real");
    }

    inline proc fetchAnd(value:real(64)):real(64) {
      compilerError("and not definted for network atomic real");
    }
    inline proc and(value:real(64)):real(64) {
      compilerError("and not definted for network atomic real");
    }

    inline proc waitFor(val:real(64)) {
      on this do while (read() != val) do chpl_task_yield();
    }

    inline proc peek() {
      return _v;
    }
    inline proc poke(value:real(64)) {
      _v = value;
    }

    proc writeThis(x: Writer) {
      x.write(read());
    }
  }

  inline proc =(ref a:ratomic_real64, b:ratomic_real64) {
    a.write(b.read());
  }
  inline proc =(ref a:ratomic_real64, b) {
    compilerError("Cannot directly assign network atomic variables");
  }
  inline proc +(a:ratomic_real64, b) {
    compilerError("Cannot directly add network atomic variables");
    return a;
  }
  inline proc -(a:ratomic_real64, b) {
    compilerError("Cannot directly subtract network atomic variables");
    return a;
  }
  inline proc *(a:ratomic_real64, b) {
    compilerError("Cannot directly multiply network atomic variables");
    return a;
  }
  inline proc /(a:ratomic_real64, b) {
    compilerError("Cannot directly divide network atomic variables");
    return a;
  }
  inline proc %(a:ratomic_real64, b) {
    compilerError("Cannot directly divide network atomic variables");
    return a;
  }

}

