import re

from capstone.x86 import X86_OP_REG

from guest_ops import FLAGS, GPR, Unsupported


def register_operand(ins, index, name):
    return (len(ins.operands) > index and ins.operands[index].type == X86_OP_REG
            and ins.reg_name(ins.operands[index].reg) == name and ins.operands[index].size == 8)


def layout(unit):
    if len(unit.instructions) > 16 or unit.calls or unit.tails or unit.indirect or unit.tables or unit.landing_pads:
        return None
    instructions = unit.instructions
    returns = [ins for ins in instructions.values() if ins.mnemonic == 'ret']
    if not returns or any(ins.operands for ins in returns):
        return None
    first = instructions[unit.start]
    if first.mnemonic != 'push' or not register_operand(first, 0, 'rbp'):
        return {'frame': False, 'entry': unit.start, 'body': tuple(sorted(instructions)), 'exit': None}
    second = instructions.get(first.address + first.size)
    if (second is None or second.mnemonic != 'mov' or not register_operand(second, 0, 'rbp')
            or not register_operand(second, 1, 'rsp') or len(returns) != 1):
        return None
    back = returns[0]
    pops = [ins for ins in instructions.values() if ins.address + ins.size == back.address]
    if len(pops) != 1 or pops[0].mnemonic != 'pop' or not register_operand(pops[0], 0, 'rbp'):
        return None
    pop = pops[0]
    if (unit.predecessors(first.address) or unit.predecessors(second.address) != [first.address]
            or unit.predecessors(back.address) != [pop.address]):
        return None
    entry = second.address + second.size
    body = tuple(address for address in sorted(instructions) if address not in (first.address, second.address, back.address))
    if entry not in body:
        return None
    return {'frame': True, 'entry': entry, 'body': body, 'exit': pop.address}


def name(address, concurrent):
    return f'@__aps5_value_leaf_{address:x}_{"ordered" if concurrent else "private"}'


def result_type(outputs):
    return 'void' if not outputs else 'i64' if len(outputs) == 1 else '{ i64, i64 }'


def emit(base_type, unit, units, contract, concurrent, ordered_access, collecting=False):
    outputs = () if collecting else contract['outputs']
    inputs = tuple(GPR) if collecting else contract['inputs']
    shape = contract['layout']

    class Leaf(base_type):
        def emit(self, text):
            if '%s' in text:
                raise Unsupported('value leaf requires external guest state')
            call = re.search(r'\bcall\b.*?@([A-Za-z_][\w.]*)', text)
            if call and not (call[1].startswith('llvm.') or call[1] == 'NativeAotGuestTrap'):
                raise Unsupported('value leaf requires an external helper')
            super().emit(text)

        def get_gpr(self, register):
            self.reads.add(register)
            return super().get_gpr(register)

        def set_gpr(self, register, value):
            if register == 'rsp' or (shape['frame'] and register == 'rbp'):
                raise Unsupported('value leaf changes its frame inside the body')
            super().set_gpr(register, value)

        def get_xmm(self, index):
            raise Unsupported('value leaf vector state is not qualified')

        def set_xmm(self, index, value):
            raise Unsupported('value leaf vector state is not qualified')

        def get_ymm_upper(self, index):
            raise Unsupported('value leaf vector state is not qualified')

        def set_ymm_upper(self, index, value):
            raise Unsupported('value leaf vector state is not qualified')

        def prologue(self):
            arguments = ', '.join(['i64 %base'] + [f'i64 %in_{register}' for register in inputs])
            self.lines.append(f'define internal {result_type(outputs)} {self.symbol}({arguments}) uwtable {{')
            self.lines.append('entry:')
            for index in range(16):
                self.emit(f'%R{index} = alloca i64')
            for register in inputs:
                self.emit(f'store i64 %in_{register}, ptr %R{GPR.index(register)}')
            for flag in FLAGS:
                self.emit(f'%F{flag} = alloca i1')
                self.emit(f'store i1 false, ptr %F{flag}')
            self.emit(f'br label %L{shape["entry"]:x}')

        def finish(self):
            if not outputs:
                self.emit('ret void')
            elif len(outputs) == 1:
                self.emit(f'ret i64 {self.get_gpr(outputs[0])}')
            else:
                result = self.v(f'insertvalue {{ i64, i64 }} poison, i64 {self.get_gpr(outputs[0])}, 0')
                result = self.v(f'insertvalue {{ i64, i64 }} {result}, i64 {self.get_gpr(outputs[1])}, 1')
                self.emit(f'ret {{ i64, i64 }} {result}')

        def control(self, ins):
            if ins.mnemonic == 'ret':
                self.finish()
            else:
                super().control(ins)

        def instruction(self, ins):
            if ins.address == shape['exit']:
                self.ins = ins
                self.finish()
            else:
                super().instruction(ins)

    lowering = Leaf(unit, name(unit.start, concurrent))
    lowering.epoch_variant = None
    lowering.sealed_tables = lowering.readonly_table_loads = set()
    lowering.writeback = None
    lowering.call_effects = {}
    lowering.value_leaves = {}
    lowering.gpr_writes, lowering.xmm_writes, lowering.ymm_writes = set(), set(), set()
    lowering.reads = set()
    lowering.units, lowering.trace, lowering.concurrent = units, False, concurrent
    lowering.ordered_access = ordered_access
    lowering.prologue()
    for address in shape['body']:
        lowering.lines.append(f'L{address:x}:')
        lowering.instruction(unit.instructions[address])
    lowering.lines.append('}')
    written = {GPR[index] for index in lowering.gpr_writes}
    if not collecting and (not lowering.reads <= set(inputs) or not written <= set(outputs)):
        raise Unsupported('value leaf register effects exceed its contract')
    return '\n'.join(lowering.lines), lowering.declarations, lowering.reads, written


def candidates(base_type, units, ordered_access):
    contracts = {}
    for address, unit in units.items():
        shape = layout(unit)
        if shape is None:
            continue
        contract = {'layout': shape}
        try:
            _, _, reads, written = emit(base_type, unit, units, contract, False, ordered_access, collecting=True)
            inputs = reads | written
            if len(written) > 2 or len(inputs) > 6:
                continue
            contract.update(inputs=tuple(register for register in GPR if register in inputs),
                            outputs=tuple(register for register in GPR if register in written))
            for mode in (False, True):
                emit(base_type, unit, units, contract, mode, ordered_access)
        except Unsupported:
            continue
        contracts[address] = contract
    return contracts


def invoke(lowering, address):
    contract = lowering.value_leaves[address]
    if contract['layout']['frame']:
        frame = lowering.v(f'sub i64 {lowering.get_gpr("rsp")}, 8')
        lowering.store(frame, lowering.get_gpr('rbp'), 64)
        lowering.set_gpr('rsp', frame)
        lowering.set_gpr('rbp', frame)
    arguments = ', '.join(['i64 %base'] + [f'i64 {lowering.get_gpr(register)}' for register in contract['inputs']])
    outputs = contract['outputs']
    call = f'call {result_type(outputs)} {name(address, lowering.concurrent)}({arguments})'
    if not outputs:
        lowering.emit(call)
    else:
        result = lowering.v(call)
        for index, register in enumerate(outputs):
            value = result if len(outputs) == 1 else lowering.v(f'extractvalue {{ i64, i64 }} {result}, {index}')
            lowering.set_gpr(register, value)
    rsp = lowering.get_gpr('rsp')
    if contract['layout']['frame']:
        lowering.set_gpr('rbp', lowering.load(rsp, 64))
        rsp = lowering.v(f'add i64 {rsp}, 8')
    target = lowering.load(rsp, 64)
    lowering.set_gpr('rsp', lowering.v(f'add i64 {rsp}, 8'))
    return target
