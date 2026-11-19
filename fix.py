#!/usr/bin/env python3
"""
fix.py - patches the PyCPP compiler sources for three pointer-related bugs:

  1. `ptr.field = value`      emitted `ptr.field = value;`      (needs `->`)
  2. `ptr.next.next`          emitted `ptr->next.next`           (needs `->` at every pointer hop)
  3. `(*ptr).field`, `a[i].x`, `f().x`  failed to parse          (no postfix operators after a primary)

Usage (run from the repository root, next to CMakeLists.txt):

    python fix.py            # apply the patches (backups saved as *.bak)
    python fix.py --check    # only report whether the patches would apply
    python fix.py <root>     # use a different repository root

The script is idempotent: files that already contain the "[fix.py]" marker are skipped.
Nothing is written unless every patch in every file matches.
"""
import shutil
import sys
from pathlib import Path

MARKER = "[fix.py]"


# ----------------------------------------------------------------------------
# helpers
# ----------------------------------------------------------------------------
class PatchError(Exception):
    pass


def load(path):
    raw = path.read_bytes()
    bom = raw.startswith(b"\xef\xbb\xbf")
    text = raw.decode("utf-8-sig")
    crlf = "\r\n" in text
    return text.replace("\r\n", "\n"), bom, crlf


def save(path, text, bom, crlf):
    if crlf:
        text = text.replace("\n", "\r\n")
    data = text.encode("utf-8")
    if bom:
        data = b"\xef\xbb\xbf" + data
    path.write_bytes(data)


def replace_once(text, old, new, label):
    n = text.count(old)
    if n != 1:
        raise PatchError(f"{label}: expected exactly 1 match, found {n}")
    return text.replace(old, new)


def replace_between(text, start, end, new, label):
    s = text.find(start)
    e = text.find(end)
    if s < 0 or e < 0 or e <= s:
        raise PatchError(f"{label}: could not locate the block")
    return text[:s] + new + text[e:]


# ----------------------------------------------------------------------------
# code_generator.h
# ----------------------------------------------------------------------------
def patch_header(t):
    t = replace_once(
        t,
        "bool hasSubclasses(const std::string& class_name);",
        "bool hasSubclasses(const std::string& class_name);\n"
        "// [fix.py] declared type of a member (searches base classes); nullptr when unknown\n"
        "Type* memberType(const std::string& class_name, const std::string& member);",
        "header: memberType declaration",
    )
    t = replace_once(
        t,
        "    bool isPointerExpr(Expr* expr);",
        """    // [fix.py] what the generator can tell about an expression's type
    struct TypeInfo {
        bool known = false;      // false: nothing could be determined
        int ptr = 0;             // pointer depth (Node* -> 1, Node** -> 2)
        std::string cls;         // class/struct name when it is one
        Type* elem = nullptr;    // element type when it is a list[T]
    };
    TypeInfo typeInfoOf(Type* type);
    TypeInfo exprTypeInfo(Expr* expr);
    void generateObjectOperand(Expr* expr);   // parenthesises (*p), a + b, ... before '.' / '->' / '['
    bool isPointerExpr(Expr* expr);""",
        "header: TypeInfo declarations",
    )
    return t


# ----------------------------------------------------------------------------
# code_generator.cpp
# ----------------------------------------------------------------------------
NEW_REGISTER = r'''void pycpp::registerClasses(Program* program) {
    // [fix.py] Also records the declared type of every member (and covers structs), so the
    // code generator can tell which member accesses go through a pointer and need '->'.
    for (auto& stmt : program->statements) {
        if (auto st = dynamic_cast<StructDef*>(stmt.get())) {
            ClassInfo& sinfo = classRegistry()[st->name];
            for (auto& f : st->fields) {
                sinfo.members.insert(f->var_name);
                if (f->type) sinfo.types[f->var_name] = f->type.get();
            }
            continue;
        }
        auto cls = dynamic_cast<ClassDef*>(stmt.get());
        if (!cls) continue;
        ClassInfo& info = classRegistry()[cls->name];
        info.base = cls->base_class;
        for (auto& attr : cls->attributes) {
            std::string n = attr->var_name;
            if (n.rfind("self.", 0) == 0) n = n.substr(5);
            info.members.insert(n);
            if (attr->type) info.types[n] = attr->type.get();
        }
        for (auto& m : cls->methods) {
            if (m->name != "__init__") continue;
            for (auto& s : m->body) {
                std::string n = selfMemberAssigned(s.get());
                if (n.empty()) continue;
                info.members.insert(n);
                if (auto v = dynamic_cast<VarDecl*>(s.get()))
                    if (v->type && !info.types.count(n)) info.types[n] = v->type.get();
            }
        }
    }
}

// [fix.py] declared type of class/struct member `member`, searching base classes
Type* pycpp::memberType(const std::string& cls, const std::string& member) {
    auto& reg = classRegistry();
    std::string current = cls;
    for (int depth = 0; depth < 64; ++depth) {
        auto it = reg.find(current);
        if (it == reg.end()) return nullptr;
        auto found = it->second.types.find(member);
        if (found != it->second.types.end()) return found->second;
        if (it->second.base.empty()) return nullptr;
        current = it->second.base;
    }
    return nullptr;
}

'''

NEW_IS_POINTER = r'''// [fix.py] Type facts about an expression: pointer depth, class name, list element type.
CodeGenerator::TypeInfo CodeGenerator::typeInfoOf(Type* type) {
    TypeInfo info;
    if (!type) return info;
    Type* cur = type;
    while (auto p = dynamic_cast<PointerType*>(cur)) {
        info.ptr++;
        cur = p->base_type.get();
        if (!cur) return info;
    }
    info.known = true;
    if (auto basic = dynamic_cast<BasicType*>(cur)) info.cls = basic->name;
    else if (auto list = dynamic_cast<ListType*>(cur)) info.elem = list->element_type.get();
    return info;
}

CodeGenerator::TypeInfo CodeGenerator::exprTypeInfo(Expr* expr) {
    TypeInfo none;
    if (!expr) return none;

    if (auto ident = dynamic_cast<Identifier*>(expr)) {
        if ((ident->name == "self" || ident->name == "this") && current_class) {
            TypeInfo self;
            self.known = true;
            self.ptr = 1;
            self.cls = current_class->name;
            return self;
        }
        return typeInfoOf(lookupVar(ident->name));
    }
    if (auto member = dynamic_cast<MemberAccess*>(expr)) {
        TypeInfo object = exprTypeInfo(member->object.get());
        if (!object.known || object.cls.empty()) return none;
        return typeInfoOf(pycpp::memberType(object.cls, member->member));
    }
    if (auto unary = dynamic_cast<UnaryOp*>(expr)) {
        TypeInfo inner = exprTypeInfo(unary->operand.get());
        if (!inner.known) return none;
        if (unary->op == "*" && inner.ptr > 0) { inner.ptr--; return inner; }   // *p
        if (unary->op == "&") { inner.ptr++; return inner; }                    // &x
        return none;
    }
    if (auto call = dynamic_cast<CallExpr*>(expr)) {
        if (call->is_subscript) {                                               // a[i]
            TypeInfo base = exprTypeInfo(call->callee.get());
            if (!base.known) return none;
            if (base.ptr > 0) { base.ptr--; return base; }
            if (base.elem) return typeInfoOf(base.elem);
        }
        return none;
    }
    return none;
}

// True when the expression is a pointer, so member access must use '->' instead of '.'.
// Works for variables, self, chained members (a.next.next), *p, &x and list elements.
bool CodeGenerator::isPointerExpr(Expr* expr) {
    TypeInfo info = exprTypeInfo(expr);
    return info.known && info.ptr > 0;
}

// Operand of '.', '->' or '[': (*p).x, (a + b).x need their parentheses back,
// because the parser drops them.
void CodeGenerator::generateObjectOperand(Expr* expr) {
    const bool wrap = dynamic_cast<UnaryOp*>(expr) || dynamic_cast<BinaryOp*>(expr) ||
                      dynamic_cast<TernaryOp*>(expr);
    if (wrap) output << "(";
    generateExpression(expr);
    if (wrap) output << ")";
}
'''


def patch_codegen(t):
    # registry: remember member types
    t = replace_once(
        t,
        "struct ClassInfo { std::string base; std::set<std::string> members; };",
        "struct ClassInfo { std::string base; std::set<std::string> members; "
        "std::map<std::string, Type*> types; /* [fix.py] */ };",
        "codegen: ClassInfo",
    )
    t = replace_between(
        t,
        "void pycpp::registerClasses(Program* program) {",
        "bool pycpp::isInheritedMember(",
        NEW_REGISTER,
        "codegen: registerClasses",
    )

    # isPointerExpr + resolver
    old_is_pointer = (
        "bool CodeGenerator::isPointerExpr(Expr* expr) {\n"
        "    if (auto ident = dynamic_cast<Identifier*>(expr)) {\n"
        "        Type* type = lookupVar(ident->name);\n"
        "        return type && dynamic_cast<PointerType*>(type) && !type->is_reference;\n"
        "    }\n"
        "    return false;\n"
        "}\n"
    )
    # keep the original explanatory comment above it intact; only replace the function
    t = replace_once(t, old_is_pointer, NEW_IS_POINTER, "codegen: isPointerExpr")

    # a[i] : the indexed object may be (*p) etc.
    t = replace_once(
        t,
        "        if (call_expr->is_subscript) {\n"
        "            generateExpression(call_expr->callee.get());\n",
        "        if (call_expr->is_subscript) {\n"
        "            generateObjectOperand(call_expr->callee.get());   // [fix.py]\n",
        "codegen: subscript callee",
    )

    # method calls: obj.method() where obj may be a chain or a deref
    t = replace_once(
        t,
        "            bool arrow = false;\n"
        "            Type* object_type = nullptr;\n"
        "            if (auto ident = dynamic_cast<Identifier*>(member_access->object.get())) {\n"
        "                arrow = (ident->name == \"self\" && current_class) || ident->name == \"this\" || isPointerExpr(ident);\n"
        "                object_type = lookupVar(ident->name);\n"
        "            }\n",
        "            bool arrow = isPointerExpr(member_access->object.get());   // [fix.py] handles chains too\n"
        "            Type* object_type = nullptr;\n"
        "            if (auto ident = dynamic_cast<Identifier*>(member_access->object.get())) {\n"
        "                if (ident->name == \"this\") arrow = true;\n"
        "                object_type = lookupVar(ident->name);\n"
        "            }\n",
        "codegen: method call arrow",
    )
    t = replace_once(
        t,
        "            generateExpression(member_access->object.get());\n"
        "            output << (arrow ? \"->\" : \".\") << method << \"(\";\n",
        "            generateObjectOperand(member_access->object.get());\n"
        "            output << (arrow ? \"->\" : \".\") << method << \"(\";\n",
        "codegen: method call operand",
    )

    # BUG 1: ptr.field = value
    t = replace_once(
        t,
        "                    // Any other identifier: emit obj.member = value\n"
        "                    generateExpression(member_access->object.get());\n"
        "                    output << \".\" << member_access->member << \" = \";\n",
        "                    // [fix.py] Any other object: the member-access generator picks '.' or '->'\n"
        "                    generateExpression(member_access);\n"
        "                    output << \" = \";\n",
        "codegen: member assignment",
    )

    # BUG 2: chained / complex objects
    t = replace_once(
        t,
        "        else {\n"
        "            // For complex expressions, use dot operator\n"
        "            generateExpression(member_access->object.get());\n"
        "            output << \".\" << member_access->member;\n"
        "        }\n",
        "        else {\n"
        "            // [fix.py] Chained / complex objects: '->' whenever the object is itself a pointer\n"
        "            generateObjectOperand(member_access->object.get());\n"
        "            output << (isPointerExpr(member_access->object.get()) ? \"->\" : \".\")\n"
        "                   << member_access->member;\n"
        "        }\n",
        "codegen: chained member access",
    )
    return t


# ----------------------------------------------------------------------------
# pycpp_parser.cpp  (BUG 3)
# ----------------------------------------------------------------------------
POSTFIX = r'''
        // [fix.py] Postfix operators after ANY primary expression, not just after a bare
        // identifier: (*p).x   a[i].x   f(x).y   obj.method().field   a[i][j]
        while (result) {
            if (check(TokenType::DOT)) {
                advance();
                if (atEnd() || !isWordToken(peek())) {
                    throw std::runtime_error("Expected identifier after '.'");
                }
                Token member = advance();
                result = std::make_unique<MemberAccess>(std::move(result), member.value,
                                                        member.line, member.column);
            }
            else if (check(TokenType::LBRACKET)) {
                Token open = advance();
                auto index = parseExpression();
                consume(TokenType::RBRACKET, "Expected ']' after index");
                auto subscript = std::make_unique<CallExpr>();
                subscript->callee = std::move(result);
                subscript->is_subscript = true;
                subscript->arguments.push_back(std::move(index));
                subscript->line = open.line;
                subscript->column = open.column;
                result = std::move(subscript);
            }
            else if (check(TokenType::LPAREN)) {
                result = parseCall(std::move(result));
            }
            else {
                break;
            }
        }
'''


def patch_parser(t):
    old = (
        "                throw std::runtime_error(\"Expected expression, got: '\" + peek().value + \"'\");\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "    catch (...) {\n"
    )
    new = (
        "                throw std::runtime_error(\"Expected expression, got: '\" + peek().value + \"'\");\n"
        "            }\n"
        "        }\n"
        + POSTFIX +
        "    }\n"
        "    catch (...) {\n"
    )
    return replace_once(t, old, new, "parser: postfix loop")


# ----------------------------------------------------------------------------
# example that exercises all three fixes
# ----------------------------------------------------------------------------
EXAMPLE = '''struct Node:
    x: int
    next: *Node

def make(x: int, nxt: *Node) -> *Node:
    n: *Node = mem.alloc[Node]()
    n.x = x              # was: n.x = x;   now: n->x = x;
    n.next = nxt
    return n

def main() -> int:
    a: *Node = make(1, nullptr)
    b: *Node = make(2, a)
    c: *Node = make(3, b)

    print(c.next.next.x)     # chained pointers: c->next->next->x   (expect 1)

    (*c).x = 30              # postfix after parentheses: (*c).x = 30
    print((*c).x)            # expect 30

    nodes: list[*Node] = [a, b, c]
    print(nodes[1].x)        # postfix after a subscript: nodes[1]->x   (expect 2)

    mem.free(c)
    mem.free(b)
    mem.free(a)
    return 0
'''

FILES = [
    ("PyCpp/include/code_generator.h", patch_header),
    ("PyCpp/src/code_generator.cpp", patch_codegen),
    ("PyCpp/src/pycpp_parser.cpp", patch_parser),
]


def main(argv):
    check_only = "--check" in argv
    args = [a for a in argv[1:] if not a.startswith("--")]
    root = Path(args[0]).resolve() if args else Path(__file__).resolve().parent

    pending = []
    for rel, fn in FILES:
        path = root / rel
        if not path.exists():
            print(f"error: {path} not found (run from the repository root)")
            return 1
        text, bom, crlf = load(path)
        if MARKER in text:
            print(f"skip   {rel} (already patched)")
            continue
        try:
            patched = fn(text)
        except PatchError as e:
            print(f"error: {rel}: {e}")
            print("       the file differs from the version this script was written against; nothing was changed.")
            return 1
        pending.append((path, rel, patched, bom, crlf))
        print(f"ok     {rel} (patch applies)")

    if check_only:
        print("check only: nothing written.")
        return 0

    for path, rel, patched, bom, crlf in pending:
        shutil.copyfile(path, str(path) + ".bak")
        save(path, patched, bom, crlf)
        print(f"wrote  {rel}   (backup: {rel}.bak)")

    example = root / "PyCpp/examples/low_level/pointer_chain.pycpp"
    if not example.exists() and example.parent.exists():
        example.write_text(EXAMPLE, encoding="utf-8", newline="\n")
        print(f"wrote  {example.relative_to(root)}")

    if pending:
        print("\nRebuild the compiler, then try:")
        print("  pycpp PyCpp/examples/low_level/pointer_chain.pycpp")
        print("Expected output: 1, 30, 2")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))