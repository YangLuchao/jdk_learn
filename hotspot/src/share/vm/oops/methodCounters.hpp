/*
 * Copyright (c) 2013, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_VM_OOPS_METHODCOUNTERS_HPP
#define SHARE_VM_OOPS_METHODCOUNTERS_HPP

#include "oops/metadata.hpp"
#include "interpreter/invocationCounter.hpp"
  /*
   MethodCounters指针，在methodCounters.hpp中定义，用于与大量编译、优化相关的技术，例如：
   1、解释器调用次数
   2、解释器执行时由于一场而终止的次数
   3、方法调用次数(method里面有多少次方法调用)
   4、回边个数
   5、Java方法运行过的分层编译的最高层级(由于HotSpot VM中存在分层编译，所以一个方法可能会被编译器编译为不同的层级，_method_counters只会记录运行过的最高层级)
   6、热点方法计数
   7、基于调用频率的热点方法的跟踪统计
  */
class MethodCounters: public MetaspaceObj {
 friend class VMStructs;
 private:
  int               _interpreter_invocation_count; // Count of times invoked (reused as prev_event_count in tiered)
  u2                _interpreter_throwout_count; // Count of times method was exited via exception while interpreting
  u2                _number_of_breakpoints;      // fullspeed debugging support
  InvocationCounter _invocation_counter;         // Incremented before each activation of the method - used to trigger frequency-based optimizations
  InvocationCounter _backedge_counter;           // Incremented before each backedge taken - used to trigger frequencey-based optimizations

#ifdef TIERED
  float             _rate;                        // Events (invocation and backedge counter increments) per millisecond
  u1                _highest_comp_level;          // Highest compile level this method has ever seen.
  u1                _highest_osr_comp_level;      // Same for OSR level
  jlong             _prev_time;                   // Previous time the rate was acquired
#endif

  MethodCounters() : _interpreter_invocation_count(0),
                     _interpreter_throwout_count(0),
                     _number_of_breakpoints(0)
#ifdef TIERED
                   , _rate(0),
                     _highest_comp_level(0),
                     _highest_osr_comp_level(0),
                     _prev_time(0)
#endif
  {
    invocation_counter()->init();
    backedge_counter()->init();
  }

 public:
  static MethodCounters* allocate(ClassLoaderData* loader_data, TRAPS);

  void deallocate_contents(ClassLoaderData* loader_data) {}
  DEBUG_ONLY(bool on_stack() { return false; })  // for template

  static int size() { return sizeof(MethodCounters) / wordSize; }

  bool is_klass() const { return false; }

  void clear_counters();

  int interpreter_invocation_count() {
    return _interpreter_invocation_count;
  }
  void set_interpreter_invocation_count(int count) {
    _interpreter_invocation_count = count;
  }
  int increment_interpreter_invocation_count() {
    return ++_interpreter_invocation_count;
  }

  void interpreter_throwout_increment() {
    if (_interpreter_throwout_count < 65534) {
      _interpreter_throwout_count++;
    }
  }
  int  interpreter_throwout_count() const {
    return _interpreter_throwout_count;
  }
  void set_interpreter_throwout_count(int count) {
    _interpreter_throwout_count = count;
  }

  u2   number_of_breakpoints() const   { return _number_of_breakpoints; }
  void incr_number_of_breakpoints()    { ++_number_of_breakpoints; }
  void decr_number_of_breakpoints()    { --_number_of_breakpoints; }
  void clear_number_of_breakpoints()   { _number_of_breakpoints = 0; }

#ifdef TIERED
  jlong prev_time() const                        { return _prev_time; }
  void set_prev_time(jlong time)                 { _prev_time = time; }
  float rate() const                             { return _rate; }
  void set_rate(float rate)                      { _rate = rate; }
#endif

  int highest_comp_level() const;
  void set_highest_comp_level(int level);
  int highest_osr_comp_level() const;
  void set_highest_osr_comp_level(int level);

  // invocation counter
  InvocationCounter* invocation_counter() { return &_invocation_counter; }
  InvocationCounter* backedge_counter()   { return &_backedge_counter; }

  static ByteSize interpreter_invocation_counter_offset() {
    return byte_offset_of(MethodCounters, _interpreter_invocation_count);
  }

  static ByteSize invocation_counter_offset()    {
    return byte_offset_of(MethodCounters, _invocation_counter);
  }

  static ByteSize backedge_counter_offset()      {
    return byte_offset_of(MethodCounters, _backedge_counter);
  }

  static int interpreter_invocation_counter_offset_in_bytes() {
    return offset_of(MethodCounters, _interpreter_invocation_count);
  }

};
#endif //SHARE_VM_OOPS_METHODCOUNTERS_HPP
