// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/change-lowering.h"

#include "src/code-factory.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/linkage.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/operator-properties.h"

namespace v8 {
namespace internal {
namespace compiler {

ChangeLowering::~ChangeLowering() {}


Reduction ChangeLowering::Reduce(Node* node) {
  Node* control = graph()->start();
  switch (node->opcode()) {
    case IrOpcode::kChangeBitToBool:
      return ChangeBitToBool(node->InputAt(0), control);
    case IrOpcode::kChangeBoolToBit:
      return ChangeBoolToBit(node->InputAt(0));
    case IrOpcode::kChangeFloat64ToTagged:
      return ChangeFloat64ToTagged(node->InputAt(0), control);
#if V8_TARGET_ARCH_IA32 || V8_TARGET_ARCH_X64
    case IrOpcode::kChangeFloat32x4ToTagged:
      return ChangeFloat32x4ToTagged(node->InputAt(0), control);
    case IrOpcode::kChangeTaggedToFloat32x4:
      return ChangeTaggedToFloat32x4(node->InputAt(0), control);
    case IrOpcode::kChangeInt32x4ToTagged:
      return ChangeInt32x4ToTagged(node->InputAt(0), control);
    case IrOpcode::kChangeTaggedToInt32x4:
      return ChangeTaggedToInt32x4(node->InputAt(0), control);
    case IrOpcode::kChangeFloat64x2ToTagged:
      return ChangeFloat64x2ToTagged(node->InputAt(0), control);
    case IrOpcode::kChangeTaggedToFloat64x2:
      return ChangeTaggedToFloat64x2(node->InputAt(0), control);
#endif
    case IrOpcode::kChangeInt32ToTagged:
      return ChangeInt32ToTagged(node->InputAt(0), control);
    case IrOpcode::kChangeTaggedToFloat64:
      return ChangeTaggedToFloat64(node->InputAt(0), control);
    case IrOpcode::kChangeTaggedToInt32:
      return ChangeTaggedToUI32(node->InputAt(0), control, kSigned);
    case IrOpcode::kChangeTaggedToUint32:
      return ChangeTaggedToUI32(node->InputAt(0), control, kUnsigned);
    case IrOpcode::kChangeUint32ToTagged:
      return ChangeUint32ToTagged(node->InputAt(0), control);
    default:
      return NoChange();
  }
  UNREACHABLE();
  return NoChange();
}


Node* ChangeLowering::HeapNumberValueIndexConstant() {
  return jsgraph()->IntPtrConstant(HeapNumber::kValueOffset - kHeapObjectTag);
}


#if V8_TARGET_ARCH_IA32 || V8_TARGET_ARCH_X64
Node* ChangeLowering::Float32x4ValueIndexConstant() {
  STATIC_ASSERT(Float32x4::kValueOffset % kPointerSize == 0);
  const int value_offset =
      ((Float32x4::kValueOffset / kPointerSize) * (machine()->Is64() ? 8 : 4));
  return jsgraph()->IntPtrConstant(value_offset - kHeapObjectTag);
}


Node* ChangeLowering::Int32x4ValueIndexConstant() {
  STATIC_ASSERT(Int32x4::kValueOffset % kPointerSize == 0);
  const int value_offset =
      ((Int32x4::kValueOffset / kPointerSize) * (machine()->Is64() ? 8 : 4));
  return jsgraph()->IntPtrConstant(value_offset - kHeapObjectTag);
}


Node* ChangeLowering::Float64x2ValueIndexConstant() {
  STATIC_ASSERT(Float64x2::kValueOffset % kPointerSize == 0);
  const int value_offset =
      ((Float64x2::kValueOffset / kPointerSize) * (machine()->Is64() ? 8 : 4));
  return jsgraph()->IntPtrConstant(value_offset - kHeapObjectTag);
}
#endif


Node* ChangeLowering::SmiMaxValueConstant() {
  return jsgraph()->Int32Constant(Smi::kMaxValue);
}


Node* ChangeLowering::SmiShiftBitsConstant() {
  return jsgraph()->IntPtrConstant(kSmiShiftSize + kSmiTagSize);
}


Node* ChangeLowering::AllocateHeapNumberWithValue(Node* value, Node* control) {
  // The AllocateHeapNumberStub does not use the context, so we can safely pass
  // in Smi zero here.
  Callable callable = CodeFactory::AllocateHeapNumber(isolate());
  Node* target = jsgraph()->HeapConstant(callable.code());
  Node* context = jsgraph()->NoContextConstant();
  Node* effect = graph()->NewNode(common()->ValueEffect(1), value);
  if (!allocate_heap_number_operator_.is_set()) {
    CallDescriptor* descriptor = Linkage::GetStubCallDescriptor(
        isolate(), jsgraph()->zone(), callable.descriptor(), 0,
        CallDescriptor::kNoFlags, Operator::kNoThrow);
    allocate_heap_number_operator_.set(common()->Call(descriptor));
  }
  Node* heap_number = graph()->NewNode(allocate_heap_number_operator_.get(),
                                       target, context, effect, control);
  Node* store = graph()->NewNode(
      machine()->Store(StoreRepresentation(kMachFloat64, kNoWriteBarrier)),
      heap_number, HeapNumberValueIndexConstant(), value, heap_number, control);
  return graph()->NewNode(common()->Finish(1), heap_number, store);
}


#if V8_TARGET_ARCH_IA32 || V8_TARGET_ARCH_X64
Node* ChangeLowering::AllocateFloat32x4WithValue(Node* value, Node* control) {
  Callable callable = CodeFactory::AllocateFloat32x4(isolate());
  CallDescriptor* descriptor = Linkage::GetStubCallDescriptor(
      isolate(), jsgraph()->zone(), callable.descriptor(), 0,
      CallDescriptor::kNoFlags);
  Node* target = jsgraph()->HeapConstant(callable.code());
  Node* context = jsgraph()->NoContextConstant();
  Node* effect = graph()->NewNode(common()->ValueEffect(1), value);
  Node* float32x4_obj = graph()->NewNode(common()->Call(descriptor), target,
                                         context, effect, control);
  Node* val_obj =
      graph()->NewNode(machine()->Load(kRepTagged), float32x4_obj,
                       Float32x4ValueIndexConstant(), float32x4_obj, control);
  Node* stored_bytes = jsgraph()->Int32Constant(16);
  Node* store = graph()->NewNode(
      machine()->Store(StoreRepresentation(kRepFloat32x4, kNoWriteBarrier)),
      val_obj, jsgraph()->IntPtrConstant(FixedTypedArrayBase::kDataOffset - 1),
      value, stored_bytes, val_obj, control);
  return graph()->NewNode(common()->Finish(1), float32x4_obj, store);
}


Node* ChangeLowering::AllocateInt32x4WithValue(Node* value, Node* control) {
  Callable callable = CodeFactory::AllocateInt32x4(isolate());
  CallDescriptor* descriptor = Linkage::GetStubCallDescriptor(
      isolate(), jsgraph()->zone(), callable.descriptor(), 0,
      CallDescriptor::kNoFlags);
  Node* target = jsgraph()->HeapConstant(callable.code());
  Node* context = jsgraph()->NoContextConstant();
  Node* effect = graph()->NewNode(common()->ValueEffect(1), value);
  Node* int32x4_obj = graph()->NewNode(common()->Call(descriptor), target,
                                       context, effect, control);
  Node* val_obj =
      graph()->NewNode(machine()->Load(kRepTagged), int32x4_obj,
                       Int32x4ValueIndexConstant(), int32x4_obj, control);
  Node* stored_bytes = jsgraph()->Int32Constant(16);
  Node* store = graph()->NewNode(
      machine()->Store(StoreRepresentation(kRepInt32x4, kNoWriteBarrier)),
      val_obj, jsgraph()->IntPtrConstant(FixedTypedArrayBase::kDataOffset - 1),
      value, stored_bytes, val_obj, control);
  return graph()->NewNode(common()->Finish(1), int32x4_obj, store);
}


Node* ChangeLowering::AllocateFloat64x2WithValue(Node* value, Node* control) {
  Callable callable = CodeFactory::AllocateFloat64x2(isolate());
  CallDescriptor* descriptor = Linkage::GetStubCallDescriptor(
      isolate(), jsgraph()->zone(), callable.descriptor(), 0,
      CallDescriptor::kNoFlags);
  Node* target = jsgraph()->HeapConstant(callable.code());
  Node* context = jsgraph()->NoContextConstant();
  Node* effect = graph()->NewNode(common()->ValueEffect(1), value);
  Node* float64x2_obj = graph()->NewNode(common()->Call(descriptor), target,
                                         context, effect, control);
  Node* val_obj =
      graph()->NewNode(machine()->Load(kRepTagged), float64x2_obj,
                       Float64x2ValueIndexConstant(), float64x2_obj, control);
  Node* stored_bytes = jsgraph()->Int32Constant(16);
  Node* store = graph()->NewNode(
      machine()->Store(StoreRepresentation(kRepFloat64x2, kNoWriteBarrier)),
      val_obj, jsgraph()->IntPtrConstant(FixedTypedArrayBase::kDataOffset - 1),
      value, stored_bytes, val_obj, control);
  return graph()->NewNode(common()->Finish(1), float64x2_obj, store);
}
#endif


Node* ChangeLowering::ChangeInt32ToFloat64(Node* value) {
  return graph()->NewNode(machine()->ChangeInt32ToFloat64(), value);
}


Node* ChangeLowering::ChangeInt32ToSmi(Node* value) {
  if (machine()->Is64()) {
    value = graph()->NewNode(machine()->ChangeInt32ToInt64(), value);
  }
  return graph()->NewNode(machine()->WordShl(), value, SmiShiftBitsConstant());
}


Node* ChangeLowering::ChangeSmiToFloat64(Node* value) {
  return ChangeInt32ToFloat64(ChangeSmiToInt32(value));
}


Node* ChangeLowering::ChangeSmiToInt32(Node* value) {
  value = graph()->NewNode(machine()->WordSar(), value, SmiShiftBitsConstant());
  if (machine()->Is64()) {
    value = graph()->NewNode(machine()->TruncateInt64ToInt32(), value);
  }
  return value;
}


Node* ChangeLowering::ChangeUint32ToFloat64(Node* value) {
  return graph()->NewNode(machine()->ChangeUint32ToFloat64(), value);
}


Node* ChangeLowering::ChangeUint32ToSmi(Node* value) {
  if (machine()->Is64()) {
    value = graph()->NewNode(machine()->ChangeUint32ToUint64(), value);
  }
  return graph()->NewNode(machine()->WordShl(), value, SmiShiftBitsConstant());
}


Node* ChangeLowering::LoadHeapNumberValue(Node* value, Node* control) {
  return graph()->NewNode(machine()->Load(kMachFloat64), value,
                          HeapNumberValueIndexConstant(), graph()->start(),
                          control);
}


Node* ChangeLowering::TestNotSmi(Node* value) {
  STATIC_ASSERT(kSmiTag == 0);
  STATIC_ASSERT(kSmiTagMask == 1);
  return graph()->NewNode(machine()->WordAnd(), value,
                          jsgraph()->IntPtrConstant(kSmiTagMask));
}


Reduction ChangeLowering::ChangeBitToBool(Node* value, Node* control) {
  return Replace(graph()->NewNode(common()->Select(kMachAnyTagged), value,
                                  jsgraph()->TrueConstant(),
                                  jsgraph()->FalseConstant()));
}


Reduction ChangeLowering::ChangeBoolToBit(Node* value) {
  return Replace(graph()->NewNode(machine()->WordEqual(), value,
                                  jsgraph()->TrueConstant()));
}


Reduction ChangeLowering::ChangeFloat64ToTagged(Node* value, Node* control) {
  return Replace(AllocateHeapNumberWithValue(value, control));
}


#if V8_TARGET_ARCH_IA32 || V8_TARGET_ARCH_X64
Reduction ChangeLowering::ChangeFloat32x4ToTagged(Node* val, Node* control) {
  return Replace(AllocateFloat32x4WithValue(val, control));
}


Reduction ChangeLowering::ChangeInt32x4ToTagged(Node* val, Node* control) {
  return Replace(AllocateInt32x4WithValue(val, control));
}


Reduction ChangeLowering::ChangeFloat64x2ToTagged(Node* val, Node* control) {
  return Replace(AllocateFloat64x2WithValue(val, control));
}
#endif


Reduction ChangeLowering::ChangeInt32ToTagged(Node* value, Node* control) {
  if (machine()->Is64() ||
      NodeProperties::GetBounds(value).upper->Is(Type::SignedSmall())) {
    return Replace(ChangeInt32ToSmi(value));
  }

  Node* add = graph()->NewNode(machine()->Int32AddWithOverflow(), value, value);

  Node* ovf = graph()->NewNode(common()->Projection(1), add);
  Node* branch =
      graph()->NewNode(common()->Branch(BranchHint::kFalse), ovf, control);

  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  Node* vtrue =
      AllocateHeapNumberWithValue(ChangeInt32ToFloat64(value), if_true);

  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
  Node* vfalse = graph()->NewNode(common()->Projection(0), add);

  Node* merge = graph()->NewNode(common()->Merge(2), if_true, if_false);
  Node* phi =
      graph()->NewNode(common()->Phi(kMachAnyTagged, 2), vtrue, vfalse, merge);

  return Replace(phi);
}


Reduction ChangeLowering::ChangeTaggedToUI32(Node* value, Node* control,
                                             Signedness signedness) {
  if (NodeProperties::GetBounds(value).upper->Is(Type::TaggedSigned())) {
    return Replace(ChangeSmiToInt32(value));
  }

  const MachineType type = (signedness == kSigned) ? kMachInt32 : kMachUint32;
  const Operator* op = (signedness == kSigned)
                           ? machine()->ChangeFloat64ToInt32()
                           : machine()->ChangeFloat64ToUint32();

  if (NodeProperties::GetBounds(value).upper->Is(Type::TaggedPointer())) {
    return Replace(graph()->NewNode(op, LoadHeapNumberValue(value, control)));
  }

  Node* check = TestNotSmi(value);
  Node* branch =
      graph()->NewNode(common()->Branch(BranchHint::kFalse), check, control);

  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  Node* vtrue = graph()->NewNode(op, LoadHeapNumberValue(value, if_true));

  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
  Node* vfalse = ChangeSmiToInt32(value);

  Node* merge = graph()->NewNode(common()->Merge(2), if_true, if_false);
  Node* phi = graph()->NewNode(common()->Phi(type, 2), vtrue, vfalse, merge);

  return Replace(phi);
}


namespace {

bool CanCover(Node* value, IrOpcode::Value opcode) {
  if (value->opcode() != opcode) return false;
  bool first = true;
  for (Edge const edge : value->use_edges()) {
    if (NodeProperties::IsControlEdge(edge)) continue;
    if (NodeProperties::IsEffectEdge(edge)) continue;
    DCHECK(NodeProperties::IsValueEdge(edge));
    if (!first) return false;
    first = false;
  }
  return true;
}

}  // namespace


Reduction ChangeLowering::ChangeTaggedToFloat64(Node* value, Node* control) {
  if (CanCover(value, IrOpcode::kJSToNumber)) {
    // ChangeTaggedToFloat64(JSToNumber(x)) =>
    //   if IsSmi(x) then ChangeSmiToFloat64(x)
    //   else let y = JSToNumber(x) in
    //     if IsSmi(y) then ChangeSmiToFloat64(y)
    //     else LoadHeapNumberValue(y)
    Node* const object = NodeProperties::GetValueInput(value, 0);
    Node* const context = NodeProperties::GetContextInput(value);
    Node* const effect = NodeProperties::GetEffectInput(value);
    Node* const control = NodeProperties::GetControlInput(value);

    const Operator* merge_op = common()->Merge(2);
    const Operator* ephi_op = common()->EffectPhi(2);
    const Operator* phi_op = common()->Phi(kMachFloat64, 2);

    Node* check1 = TestNotSmi(object);
    Node* branch1 =
        graph()->NewNode(common()->Branch(BranchHint::kFalse), check1, control);

    Node* if_true1 = graph()->NewNode(common()->IfTrue(), branch1);
    Node* vtrue1 =
        FLAG_turbo_deoptimization
            ? graph()->NewNode(value->op(), object, context,
                               NodeProperties::GetFrameStateInput(value, 0),
                               effect, if_true1)
            : graph()->NewNode(value->op(), object, context, effect, if_true1);
    Node* etrue1 = vtrue1;
    {
      Node* check2 = TestNotSmi(vtrue1);
      Node* branch2 = graph()->NewNode(common()->Branch(), check2, if_true1);

      Node* if_true2 = graph()->NewNode(common()->IfTrue(), branch2);
      Node* vtrue2 = LoadHeapNumberValue(vtrue1, if_true2);

      Node* if_false2 = graph()->NewNode(common()->IfFalse(), branch2);
      Node* vfalse2 = ChangeSmiToFloat64(vtrue1);

      if_true1 = graph()->NewNode(merge_op, if_true2, if_false2);
      vtrue1 = graph()->NewNode(phi_op, vtrue2, vfalse2, if_true1);
    }

    Node* if_false1 = graph()->NewNode(common()->IfFalse(), branch1);
    Node* vfalse1 = ChangeSmiToFloat64(object);
    Node* efalse1 = effect;

    Node* merge1 = graph()->NewNode(merge_op, if_true1, if_false1);
    Node* ephi1 = graph()->NewNode(ephi_op, etrue1, efalse1, merge1);
    Node* phi1 = graph()->NewNode(phi_op, vtrue1, vfalse1, merge1);

    NodeProperties::ReplaceWithValue(value, phi1, ephi1, merge1);
    return Replace(phi1);
  }

  Node* check = TestNotSmi(value);
  Node* branch =
      graph()->NewNode(common()->Branch(BranchHint::kFalse), check, control);

  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  Node* vtrue = LoadHeapNumberValue(value, if_true);

  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
  Node* vfalse = ChangeSmiToFloat64(value);

  Node* merge = graph()->NewNode(common()->Merge(2), if_true, if_false);
  Node* phi =
      graph()->NewNode(common()->Phi(kMachFloat64, 2), vtrue, vfalse, merge);

  return Replace(phi);
}


#if V8_TARGET_ARCH_IA32 || V8_TARGET_ARCH_X64
Reduction ChangeLowering::ChangeTaggedToFloat32x4(Node* value, Node* control) {
  // TODO(weiliang): inline Float32x4 map check.
  Node* val_obj = graph()->NewNode(machine()->Load(kRepTagged), value,
                                   Float32x4ValueIndexConstant(),
                                   graph()->start(), control);

  Node* loaded_bytes = jsgraph()->Int32Constant(16);
  Node* load = graph()->NewNode(
      machine()->Load(kRepFloat32x4), val_obj,
      jsgraph()->IntPtrConstant(FixedTypedArrayBase::kDataOffset - 1),
      loaded_bytes, graph()->start(), control);

  return Replace(load);
}


Reduction ChangeLowering::ChangeTaggedToInt32x4(Node* value, Node* control) {
  // TODO(weiliang): inline Int32x4 map check.
  Node* val_obj =
      graph()->NewNode(machine()->Load(kRepTagged), value,
                       Int32x4ValueIndexConstant(), graph()->start(), control);

  Node* loaded_bytes = jsgraph()->Int32Constant(16);
  Node* load = graph()->NewNode(
      machine()->Load(kRepInt32x4), val_obj,
      jsgraph()->IntPtrConstant(FixedTypedArrayBase::kDataOffset - 1),
      loaded_bytes, graph()->start(), control);

  return Replace(load);
}


Reduction ChangeLowering::ChangeTaggedToFloat64x2(Node* value, Node* control) {
  // TODO(weiliang): inline Float6f4x2 map check.
  Node* val_obj = graph()->NewNode(machine()->Load(kRepTagged), value,
                                   Float64x2ValueIndexConstant(),
                                   graph()->start(), control);

  Node* loaded_bytes = jsgraph()->Int32Constant(16);
  Node* load = graph()->NewNode(
      machine()->Load(kRepFloat64x2), val_obj,
      jsgraph()->IntPtrConstant(FixedTypedArrayBase::kDataOffset - 1),
      loaded_bytes, graph()->start(), control);

  return Replace(load);
}
#endif


Reduction ChangeLowering::ChangeUint32ToTagged(Node* value, Node* control) {
  if (NodeProperties::GetBounds(value).upper->Is(Type::UnsignedSmall())) {
    return Replace(ChangeUint32ToSmi(value));
  }

  Node* check = graph()->NewNode(machine()->Uint32LessThanOrEqual(), value,
                                 SmiMaxValueConstant());
  Node* branch =
      graph()->NewNode(common()->Branch(BranchHint::kTrue), check, control);

  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  Node* vtrue = ChangeUint32ToSmi(value);

  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
  Node* vfalse =
      AllocateHeapNumberWithValue(ChangeUint32ToFloat64(value), if_false);

  Node* merge = graph()->NewNode(common()->Merge(2), if_true, if_false);
  Node* phi =
      graph()->NewNode(common()->Phi(kMachAnyTagged, 2), vtrue, vfalse, merge);

  return Replace(phi);
}


Isolate* ChangeLowering::isolate() const { return jsgraph()->isolate(); }


Graph* ChangeLowering::graph() const { return jsgraph()->graph(); }


CommonOperatorBuilder* ChangeLowering::common() const {
  return jsgraph()->common();
}


MachineOperatorBuilder* ChangeLowering::machine() const {
  return jsgraph()->machine();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
