import hashlib

from elftools.dwarf.descriptions import describe_form_class
from elftools.dwarf.dwarf_expr import DWARFExprParser

from recover import load_elf, object_mappings, symbol_candidates


def require(condition, message):
    if not condition:
        raise ValueError(message)


def underlying(die):
    seen = set()
    while die.tag in ('DW_TAG_typedef', 'DW_TAG_const_type', 'DW_TAG_volatile_type', 'DW_TAG_restrict_type'):
        require(die.offset not in seen, 'cyclic debug type')
        seen.add(die.offset)
        die = die.get_DIE_from_attribute('DW_AT_type')
    return die


def scalar(die):
    die = underlying(die)
    require('DW_AT_address_class' not in die.attributes, 'nondefault debug address class')
    size = die.attributes.get('DW_AT_byte_size')
    if die.tag == 'DW_TAG_pointer_type':
        require(size is None or size.value == 8, 'non-64-bit debug pointer')
        return 'ptr'
    require(die.tag == 'DW_TAG_base_type' and size is not None, 'non-scalar debug ABI requires qualification')
    encoding = die.attributes['DW_AT_encoding'].value
    prefix = {5: 'i', 6: 'i', 7: 'u', 8: 'u', 4: 'f'}.get(encoding)
    require(prefix is not None and size.value in ((4, 8) if prefix == 'f' else (1, 2, 4, 8)),
            'unsupported debug scalar encoding or width')
    return prefix + str(size.value * 8)


def object_size(die, seen=()):
    require(die.offset not in seen and len(seen) < 32, 'recursive or excessively nested debug layout')
    seen = (*seen, die.offset)
    die = underlying(die)
    if die.tag == 'DW_TAG_structure_type':
        size = die.attributes.get('DW_AT_byte_size')
        require(size is not None and describe_form_class(size.form) == 'constant' and 0 < size.value <= 1048576,
                'missing or invalid structure extent')
        ranges = []
        for member in die.iter_children():
            require(member.tag == 'DW_TAG_member' and not any(k in member.attributes for k in
                    ('DW_AT_bit_size', 'DW_AT_bit_offset', 'DW_AT_data_bit_offset')), 'nonplain structure layout')
            offset = member.attributes.get('DW_AT_data_member_location')
            require(offset is not None and describe_form_class(offset.form) == 'constant', 'dynamic member location')
            end = offset.value + object_size(member.get_DIE_from_attribute('DW_AT_type'), seen)
            require(0 <= offset.value < end <= size.value, 'member outside structure')
            ranges.append((offset.value, end))
        ranges.sort()
        require(ranges and all(a[1] <= b[0] for a, b in zip(ranges, ranges[1:])), 'overlapping structure members')
        return size.value
    if die.tag != 'DW_TAG_array_type':
        kind = scalar(die)
        return 8 if kind == 'ptr' else int(kind[1:]) // 8
    require(not any(key in die.attributes for key in ('DW_AT_byte_stride', 'DW_AT_bit_stride', 'DW_AT_GNU_vector')),
            'noncontiguous or vector array layout')
    dimensions = list(die.iter_children())
    require(len(dimensions) == 1 and dimensions[0].tag == 'DW_TAG_subrange_type', 'nonlinear array layout')
    attributes = dimensions[0].attributes
    require('DW_AT_lower_bound' not in attributes or attributes['DW_AT_lower_bound'].value == 0,
            'nonzero array lower bound')
    count = attributes.get('DW_AT_count')
    upper = attributes.get('DW_AT_upper_bound')
    require((count is None) != (upper is None), 'ambiguous or missing array extent')
    bound = count or upper
    require(describe_form_class(bound.form) == 'constant', 'dynamic array extent')
    length = bound.value if count else bound.value + 1
    require(type(length) is int and 0 < length <= 1048576, 'invalid array extent')
    size = length * object_size(die.get_DIE_from_attribute('DW_AT_type'), seen)
    require(size <= 1048576, 'debug object exceeds frame limit')
    return size


def descendants(die):
    for child in die.iter_children():
        yield child
        if child.tag == 'DW_TAG_lexical_block':
            yield from descendants(child)


def recover_debug_candidates(source, companion):
    raw, image = load_elf(source)
    obj, evidence, mappings = object_mappings(image, companion)
    require(obj.get_section_by_name('.debug_info') is not None, 'companion has no DWARF type information')
    dwarf = obj.get_dwarf_info()
    parser = DWARFExprParser(dwarf.structs)
    symbols = symbol_candidates(obj, mappings)
    result = []
    for unit in dwarf.iter_CUs():
        require(unit['version'] == 4 and unit['address_size'] == 8, 'requires 64-bit DWARF 4')
        language = unit.get_top_DIE().attributes.get('DW_AT_language')
        require(language is not None and language.value in (1, 2, 12, 29), 'requires C debug language')
        for die in unit.iter_DIEs():
            if die.tag != 'DW_TAG_subprogram' or 'DW_AT_low_pc' not in die.attributes:
                continue
            name = die.attributes['DW_AT_name'].value.decode()
            entry = {'symbol': name, 'evidence': {'companion': evidence, 'die_offset': die.offset},
                     'obligations': []}
            try:
                matching = [symbol for symbol in symbols if symbol['symbol'] == name]
                require(len(matching) == 1, 'missing or ambiguous companion symbol')
                match = matching[0]
                high = die.attributes.get('DW_AT_high_pc')
                require(high is not None and describe_form_class(high.form) == 'constant'
                        and high.value == match['size'], 'debug range differs from symbol extent')
                low = die.attributes['DW_AT_low_pc'].value
                locations = [m['address'] + low for m in mappings.values() if low + high.value <= m['size']]
                require(locations == [match['address']], 'debug offset has ambiguous executable section')
                require('DW_AT_ranges' not in die.attributes and not die.attributes.get('DW_AT_inline'),
                        'discontiguous or inline debug function')
                convention = die.attributes.get('DW_AT_calling_convention')
                require(convention is None or convention.value == 1, 'nonstandard debug calling convention')
                require(die.attributes.get('DW_AT_prototyped') and die.attributes['DW_AT_prototyped'].value,
                        'unprototyped debug function')
                children = list(die.iter_children())
                require(not any(child.tag == 'DW_TAG_unspecified_parameters' for child in children), 'variadic debug ABI')
                parameters = [scalar(child.get_DIE_from_attribute('DW_AT_type')) for child in children
                              if child.tag == 'DW_TAG_formal_parameter']
                result_type = scalar(die.get_DIE_from_attribute('DW_AT_type')) if 'DW_AT_type' in die.attributes else 'void'
                frame = parser.parse_expr(die.attributes['DW_AT_frame_base'].value)
                require(len(frame) == 1 and frame[0].op_name == 'DW_OP_reg6', 'requires fixed rbp debug frame')
                segment = next(s for s in image.iter_segments() if s['p_type'] == 'PT_LOAD'
                               and s['p_vaddr'] <= match['address'] < s['p_vaddr'] + s['p_filesz'])
                offset = match['address'] - segment['p_vaddr']
                require(segment.data()[offset:offset + 4] == bytes.fromhex('554889e5'), 'unqualified rbp prologue')
                objects = []
                for child in descendants(die):
                    if child.tag not in ('DW_TAG_variable', 'DW_TAG_formal_parameter'):
                        continue
                    location = child.attributes.get('DW_AT_location')
                    require(location is not None and describe_form_class(location.form) == 'exprloc',
                            'variable requires location-list or optimized-location qualification')
                    expression = parser.parse_expr(location.value)
                    require(len(expression) == 1 and expression[0].op_name == 'DW_OP_fbreg', 'non-frame debug object')
                    size = object_size(child.get_DIE_from_attribute('DW_AT_type'))
                    offset = expression[0].args[0] - 8
                    require(-1048576 <= offset and offset + size <= -8, 'debug object overlaps return address or saved rbp')
                    objects.append({'offset': offset, 'size': size, 'alignment': 1})
                objects.sort(key=lambda value: value['offset'])
                require(all(left['offset'] + left['size'] <= right['offset'] for left, right in zip(objects, objects[1:])),
                        'overlapping debug objects require lifetime qualification')
                entry['candidate'] = {'name': 'guest_' + name, 'address': match['address'], 'size': match['size'],
                                      'parameters': parameters, 'result': result_type, 'stack_objects': objects,
                                      'guest_calling_convention': 'x86_64-sysv'}
            except (ValueError, KeyError) as error:
                entry['obligations'].append(str(error))
            result.append(entry)
    return {'schema': 'anyps5.aot.debug-candidates.v1', 'input_sha256': hashlib.sha256(raw).hexdigest(), 'functions': result,
            'limitations': ['Matching bytes and debug records are evidence, not a semantic proof.',
                            'Caller must qualify compiler flags, ABI, debug provenance and pointer lifetimes.',
                            'No inference for stripped input, location lists, aggregate ABI or overlapping objects.']}
