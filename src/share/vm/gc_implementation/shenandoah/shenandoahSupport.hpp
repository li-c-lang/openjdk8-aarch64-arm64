/*
 * Copyright (c) 2015, 2018, Red Hat, Inc. All rights reserved.
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

#ifndef SHARE_GC_SHENANDOAH_C2_SHENANDOAHSUPPORT_HPP
#define SHARE_GC_SHENANDOAH_C2_SHENANDOAHSUPPORT_HPP

#include "gc_implementation/shenandoah/shenandoahBrooksPointer.hpp"
#include "memory/allocation.hpp"
#include "opto/addnode.hpp"
#include "opto/graphKit.hpp"
#include "opto/machnode.hpp"
#include "opto/memnode.hpp"
#include "opto/multnode.hpp"
#include "opto/node.hpp"

class IdealLoopTree;
class PhaseGVN;
class MemoryGraphFixer;

class ShenandoahBarrierC2Support : public AllStatic {
private:
#ifdef ASSERT
  enum verify_type {
    ShenandoahLoad,
    ShenandoahStore,
    ShenandoahValue,
    ShenandoahOopStore,
    ShenandoahNone
  };

  static bool verify_helper(Node* in, Node_Stack& phis, VectorSet& visited, verify_type t, bool trace, Unique_Node_List& barriers_used);
  static void report_verify_failure(const char* msg, Node* n1 = NULL, Node* n2 = NULL);
public:
  static void verify_raw_mem(RootNode* root);
private:
#endif
  static Node* dom_mem(Node* mem, Node* ctrl, int alias, Node*& mem_ctrl, PhaseIdealLoop* phase);
  static Node* no_branches(Node* c, Node* dom, bool allow_one_proj, PhaseIdealLoop* phase);
  static bool is_heap_state_test(Node* iff, int mask);
  static bool try_common_gc_state_load(Node *n, PhaseIdealLoop *phase);
  static bool has_safepoint_between(Node* start, Node* stop, PhaseIdealLoop *phase);
  static Node* find_bottom_mem(Node* ctrl, PhaseIdealLoop* phase);
  static void follow_barrier_uses(Node* n, Node* ctrl, Unique_Node_List& uses, PhaseIdealLoop* phase);
  static void test_null(Node*& ctrl, Node* val, Node*& null_ctrl, PhaseIdealLoop* phase);
  static void test_heap_stable(Node*& ctrl, Node* raw_mem, Node*& heap_stable_ctrl,
                               PhaseIdealLoop* phase);
  static void call_lrb_stub(Node*& ctrl, Node*& val, Node*& result_mem, Node* raw_mem, PhaseIdealLoop* phase);
  static Node* clone_null_check(Node*& c, Node* val, Node* unc_ctrl, PhaseIdealLoop* phase);
  static void fix_null_check(Node* unc, Node* unc_ctrl, Node* new_unc_ctrl, Unique_Node_List& uses,
                             PhaseIdealLoop* phase);
  static void in_cset_fast_test(Node*& ctrl, Node*& not_cset_ctrl, Node* val, Node* raw_mem, PhaseIdealLoop* phase);
  static void move_heap_stable_test_out_of_loop(IfNode* iff, PhaseIdealLoop* phase);
  static void merge_back_to_back_tests(Node* n, PhaseIdealLoop* phase);
  static bool identical_backtoback_ifs(Node *n, PhaseIdealLoop* phase);
  static void fix_ctrl(Node* barrier, Node* region, const MemoryGraphFixer& fixer, Unique_Node_List& uses, Unique_Node_List& uses_to_ignore, uint last, PhaseIdealLoop* phase);
  static IfNode* find_unswitching_candidate(const IdealLoopTree *loop, PhaseIdealLoop* phase);

public:
  static bool is_dominator(Node* d_c, Node* n_c, Node* d, Node* n, PhaseIdealLoop* phase);
  static bool is_dominator_same_ctrl(Node* c, Node* d, Node* n, PhaseIdealLoop* phase);

  static bool is_gc_state_load(Node* n);
  static bool is_heap_stable_test(Node* iff);

  static bool expand(Compile* C, PhaseIterGVN& igvn);
  static void pin_and_expand(PhaseIdealLoop* phase);
  static void optimize_after_expansion(VectorSet& visited, Node_Stack& nstack, Node_List& old_new, PhaseIdealLoop* phase);

#ifdef ASSERT
  static void verify(RootNode* root);
#endif
};

class MemoryGraphFixer : public ResourceObj {
private:
  Node_List _memory_nodes;
  int _alias;
  PhaseIdealLoop* _phase;
  bool _include_lsm;

  void collect_memory_nodes();
  Node* get_ctrl(Node* n) const;
  Node* ctrl_or_self(Node* n) const;
  bool mem_is_valid(Node* m, Node* c) const;
  MergeMemNode* allocate_merge_mem(Node* mem, Node* rep_proj, Node* rep_ctrl) const;
  MergeMemNode* clone_merge_mem(Node* u, Node* mem, Node* rep_proj, Node* rep_ctrl, DUIterator& i) const;
  void fix_memory_uses(Node* mem, Node* replacement, Node* rep_proj, Node* rep_ctrl) const;
  bool should_process_phi(Node* phi) const;
  bool has_mem_phi(Node* region) const;

public:
  MemoryGraphFixer(int alias, bool include_lsm, PhaseIdealLoop* phase) :
    _alias(alias), _phase(phase), _include_lsm(include_lsm) {
    assert(_alias != Compile::AliasIdxBot, "unsupported");
    collect_memory_nodes();
  }

  Node* find_mem(Node* ctrl, Node* n) const;
  void fix_mem(Node* ctrl, Node* region, Node* mem, Node* mem_for_ctrl, Node* mem_phi, Unique_Node_List& uses);
  int alias() const { return _alias; }
};

class ShenandoahCompareAndSwapPNode : public CompareAndSwapPNode {
public:
  ShenandoahCompareAndSwapPNode(Node *c, Node *mem, Node *adr, Node *val, Node *ex)
    : CompareAndSwapPNode(c, mem, adr, val, ex) { }

  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape) {
    if (in(ExpectedIn) != NULL && phase->type(in(ExpectedIn)) == TypePtr::NULL_PTR) {
      return new (phase->C) CompareAndSwapPNode(in(MemNode::Control), in(MemNode::Memory), in(MemNode::Address), in(MemNode::ValueIn), in(ExpectedIn));
    }
    return NULL;
  }

  virtual int Opcode() const;
};

class ShenandoahCompareAndSwapNNode : public CompareAndSwapNNode {
public:
  ShenandoahCompareAndSwapNNode(Node *c, Node *mem, Node *adr, Node *val, Node *ex)
    : CompareAndSwapNNode(c, mem, adr, val, ex) { }

  virtual Node *Ideal(PhaseGVN *phase, bool can_reshape) {
    if (in(ExpectedIn) != NULL && phase->type(in(ExpectedIn)) == TypeNarrowOop::NULL_PTR) {
      return new (phase->C) CompareAndSwapNNode(in(MemNode::Control), in(MemNode::Memory), in(MemNode::Address), in(MemNode::ValueIn), in(ExpectedIn));
    }
    return NULL;
  }

  virtual int Opcode() const;
};

class ShenandoahLoadReferenceBarrierNode : public Node {
public:
  enum {
    Control,
    ValueIn
  };

  enum Strength {
    NONE, WEAK, STRONG, NA
  };

  ShenandoahLoadReferenceBarrierNode(Node* ctrl, Node* val);

  virtual int Opcode() const;
  virtual const Type* bottom_type() const;
  virtual const Type* Value(PhaseTransform *phase) const;
  virtual const class TypePtr *adr_type() const { return TypeOopPtr::BOTTOM; }
  virtual uint match_edge(uint idx) const {
    return idx >= ValueIn;
  }
  virtual uint ideal_reg() const { return Op_RegP; }

  virtual Node* Identity(PhaseTransform *phase);

  uint size_of() const {
    return sizeof(*this);
  }

  Strength get_barrier_strength();
  CallStaticJavaNode* pin_and_expand_null_check(PhaseIterGVN& igvn);

private:
  bool needs_barrier(PhaseTransform* phase, Node* n);
  bool needs_barrier_impl(PhaseTransform* phase, Node* n, Unique_Node_List &visited);
};


#endif // SHARE_GC_SHENANDOAH_C2_SHENANDOAHSUPPORT_HPP
