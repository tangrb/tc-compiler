"""Machine-checkable 0.0.46 diagnostic code table.

The 80 codes are copied from standard section 12.3.  There is no runtime
dependency on the documentation: the selftest checks the frozen counts
(65 TC_CE_* + 15 TC_RE_*) and the runner uses this table to reject unknown
codes and inconsistent expected stages.
"""

DIAGNOSTIC_CODES = {
    "TC_CE_SYNTAX": ("SyntaxError", "LEX/SYN"),
    "TC_CE_MISSING_END": ("MissingEnd", "SYN"),
    "TC_CE_MODULE_LAYER": ("ModuleLayerError", "SYN"),
    "TC_CE_MISSING_VISIBILITY": ("MissingVisibility", "SYN"),
    "TC_CE_PROGRAM_MODE_MISUSE": ("ProgramModeMisuse", "SYN"),
    "TC_CE_VAR_MISSING_INIT": ("VarMissingInitializer", "SYN"),
    "TC_CE_LITERAL_LEX_RANGE": ("LiteralLexRange", "LEX"),
    "TC_CE_UNDEFINED_NAME": ("UndefinedName", "SEM"),
    "TC_CE_DUPLICATE_NAME": ("DuplicateName", "SEM"),
    "TC_CE_DUPLICATE_MEMBER": ("DuplicateMember", "SEM"),
    "TC_CE_DUPLICATE_PARAMETER": ("DuplicateParameter", "SEM"),
    "TC_CE_FUNCTION_SCOPE_ACCESS": ("FunctionScopeAccess", "SEM"),
    "TC_CE_UNDEFINED_FUNCTION": ("UndefinedFunction", "SEM"),
    "TC_CE_PRIVATE_ACCESS": ("PrivateAccess", "SEM"),
    "TC_CE_PRIVATE_TYPE": ("PrivateType", "SEM"),
    "TC_CE_IMPORT_NOT_FOUND": ("ImportNotFound", "SEM"),
    "TC_CE_IMPORT_NOT_LIB": ("ImportNotLib", "SEM"),
    "TC_CE_DUPLICATE_IMPORT": ("DuplicateImport", "SEM"),
    "TC_CE_CIRCULAR_IMPORT": ("CircularImport", "SEM"),
    "TC_CE_IMPORT_NAME_CONFLICT": ("ImportNameConflict", "SEM"),
    "TC_CE_DUPLICATE_LABEL": ("DuplicateLabel", "SEM"),
    "TC_CE_LABEL_NOT_FOUND": ("LabelNotFound", "SEM"),
    "TC_CE_TYPE_MISMATCH": ("TypeMismatch", "SEM"),
    "TC_CE_LITERAL_TYPE": ("LiteralType", "SEM"),
    "TC_CE_LITERAL_RANGE": ("LiteralRange", "SEM"),
    "TC_CE_MODE": ("ModeError", "SEM"),
    "TC_CE_ANNOTATION": ("AnnotationError", "SEM"),
    "TC_CE_OPERAND_COUNT": ("OperandCountError", "SEM"),
    "TC_CE_CONDITION_TYPE": ("ConditionType", "SEM"),
    "TC_CE_RETURN_TYPE": ("ReturnType", "SEM"),
    "TC_CE_RETURN_FORM": ("ReturnForm", "SEM"),
    "TC_CE_RETURN_OUTSIDE_FUNCTION": ("ReturnOutsideFunction", "SEM"),
    "TC_CE_MISSING_RETURN": ("MissingReturn", "SEM"),
    "TC_CE_ARGUMENT_COUNT": ("ArgumentCount", "SEM"),
    "TC_CE_ARGUMENT_ORDER": ("ArgumentOrder", "SEM"),
    "TC_CE_UNKNOWN_ARGUMENT": ("UnknownArgument", "SEM"),
    "TC_CE_DUPLICATE_ARGUMENT": ("DuplicateArgument", "SEM"),
    "TC_CE_MISSING_ARGUMENT": ("MissingArgument", "SEM"),
    "TC_CE_UNKNOWN_FIELD": ("UnknownField", "SEM"),
    "TC_CE_MISSING_FIELD": ("MissingField", "SEM"),
    "TC_CE_DUPLICATE_FIELD": ("DuplicateField", "SEM"),
    "TC_CE_STRUCT_SELF_VALUE": ("StructSelfValue", "SEM"),
    "TC_CE_EMPTY_STRUCT": ("EmptyStruct", "SEM"),
    "TC_CE_LAYOUT_OVERFLOW": ("LayoutOverflow", "SEM"),
    "TC_CE_FIELD_ORDER": ("FieldOrder", "SEM"),
    "TC_CE_IMMUTABLE_FIELD": ("ImmutableField", "SEM"),
    "TC_CE_CONST_ASSIGNMENT": ("ConstAssignment", "SEM"),
    "TC_CE_PARAMETER_ASSIGNMENT": ("ParameterAssignment", "SEM"),
    "TC_CE_UNINITIALIZED": ("Uninitialized", "SEM"),
    "TC_CE_ADDR_OF_CONST": ("AddrOfConst", "SEM"),
    "TC_CE_BITCAST_WIDTH": ("BitcastWidth", "SEM"),
    "TC_CE_TRUNC_WIDTH": ("TruncWidth", "SEM"),
    "TC_CE_SWITCH_TYPE": ("SwitchType", "SEM"),
    "TC_CE_VOID": ("VoidType", "SEM"),
    "TC_CE_UNSAFE_OPERATION": ("UnsafeOperation", "SEM"),
    "TC_CE_UNSAFE_CALL": ("UnsafeCall", "SEM"),
    "TC_CE_FAST_CALL": ("FastCall", "SEM"),
    "TC_CE_SAFE_VIOLATION": ("SafeViolation", "SEM"),
    "TC_CE_CONST_FORM": ("ConstForm", "SEM"),
    "TC_CE_CONST_OVERFLOW": ("ConstOverflow", "CT"),
    "TC_CE_CONST_DIV_ZERO": ("ConstDivZero", "CT"),
    "TC_CE_CONST_SHIFT": ("ConstShift", "CT"),
    "TC_CE_CONST_FLOAT_INVALID": ("ConstFloatInvalid", "CT"),
    "TC_CE_CONST_FLOAT_UNDERFLOW": ("ConstFloatUnderflow", "CT"),
    "TC_CE_CONST_CAST": ("ConstCast", "CT"),
    "TC_RE_INTEGER_OVERFLOW": ("IntegerOverflow", "RT"),
    "TC_RE_DIV_ZERO": ("DivisionByZero", "RT"),
    "TC_RE_NEGATIVE_SHIFT": ("NegativeShift", "RT"),
    "TC_RE_FLOAT_OVERFLOW": ("FloatOverflow", "RT"),
    "TC_RE_FLOAT_UNDERFLOW": ("FloatUnderflow", "RT"),
    "TC_RE_FLOAT_INVALID": ("FloatInvalidOperation", "RT"),
    "TC_RE_CAST_OVERFLOW": ("CastOverflow", "RT"),
    "TC_RE_NULL_POINTER": ("NullPointer", "RT"),
    "TC_RE_PTRDIFF_OVERFLOW": ("PtrdiffOverflow", "RT"),
    "TC_RE_WRITE_PROTECTED": ("WriteProtected", "RT"),
    "TC_RE_ADDR_UNAVAILABLE": ("AddrUnavailable", "RT"),
    "TC_RE_STACK_OVERFLOW": ("StackOverflow", "RT"),
    "TC_RE_NULL_FUNCTION_POINTER": ("NullFunctionPointer", "RT"),
    "TC_RE_IO": ("IOError", "RT"),
    "TC_RE_TRAP": ("Trap", "RT"),
}

CE_CODES = frozenset(c for c in DIAGNOSTIC_CODES if c.startswith("TC_CE_"))
RE_CODES = frozenset(c for c in DIAGNOSTIC_CODES if c.startswith("TC_RE_"))

# IMPORT codes are listed under SEM in the standard table but must be reported
# in the IMPORT phase (standard section 12.2).
IMPORT_CODES = frozenset({
    "TC_CE_IMPORT_NOT_FOUND",
    "TC_CE_IMPORT_NOT_LIB",
    "TC_CE_DUPLICATE_IMPORT",
    "TC_CE_CIRCULAR_IMPORT",
    "TC_CE_IMPORT_NAME_CONFLICT",
})

STATIC_STAGES = frozenset({"LEX", "SYN", "LEX/SYN", "IMPORT", "SEM", "CT"})
ALL_STAGES = STATIC_STAGES | {"RT"}

TERMINATIONS = frozenset({"normal", "trap", "timeout", "signal", "abnormal"})
DEVICE_POLICIES = frozenset({"reject", "read", "readwrite", "volatile"})
COMPARE_ITEMS = frozenset({
    "stdout_bytes", "trap_code", "exit_status", "termination",
})
NORMALIZE_RULES = frozenset({"address_literals", "stack_offsets_unobservable"})


def code_stage(code):
    """Return the stage listed for a code, or None for an unknown code."""
    entry = DIAGNOSTIC_CODES.get(code)
    return entry[1] if entry else None


def expected_stage_matches(code, stage):
    """True when `stage` is a legal expectation for diagnostic `code`.

    The standard table labels IMPORT codes as SEM, but the mandated report
    phase is IMPORT.  LEX/SYN is a combined label and may be specialized to
    either concrete phase.
    """
    listed = code_stage(code)
    if listed is None:
        return False
    if code in IMPORT_CODES:
        return stage in {"IMPORT", "SEM"}
    if listed == "LEX/SYN":
        return stage in {"LEX", "SYN", "LEX/SYN"}
    return listed == stage


def actual_stage_matches(code, expected, actual):
    """Compare a reported stage with the case expectation."""
    if expected == "LEX/SYN" and actual in {"LEX", "SYN"}:
        return True
    return expected == actual
