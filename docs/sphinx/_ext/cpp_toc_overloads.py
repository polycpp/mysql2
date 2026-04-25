"""Sphinx extension for Breathe/Doxygen documentation improvements.

1. Shows full parameter signatures in TOC for C++ functions (instead of
   Breathe's default empty ``()``).
2. Adds ``:sort:`` support to ``doxygenclass`` directive for alphabetical
   member ordering within sections.
"""

from docutils.parsers.rst import directives
from breathe.directives.class_like import DoxygenClassDirective
from breathe.renderer.sphinxrenderer import SphinxRenderer


def _extract_params(argsstring: str) -> str:
    """Extract the parenthesized parameter list from a Doxygen argsstring.

    ``(const std::regex &re) const`` -> ``(const std::regex &re)``
    ``()``                           -> ``()``
    """
    if not argsstring:
        return "()"

    paren_start = argsstring.find("(")
    paren_end = argsstring.rfind(")")
    if paren_start == -1 or paren_end == -1:
        return "()"

    return argsstring[paren_start : paren_end + 1]


_orig_handle_declaration = SphinxRenderer.handle_declaration


def _patched_handle_declaration(self, node, declaration, **kwargs):
    result = _orig_handle_declaration(self, node, declaration, **kwargs)

    # After Breathe sets _toc_name with "()", replace with full params
    if len(result) >= 2:
        import sphinx.addnodes as addnodes

        desc = result[1]
        if isinstance(desc, addnodes.desc) and len(desc) >= 1:
            sig = desc[0]
            if isinstance(sig, addnodes.desc_signature):
                toc_name = sig.attributes.get("_toc_name", "")
                if toc_name.endswith("()") and hasattr(node, "argsstring"):
                    base_name = toc_name[:-2]
                    params = _extract_params(node.argsstring)
                    sig.attributes["_toc_name"] = base_name + params

    return result


def setup(app):
    SphinxRenderer.handle_declaration = _patched_handle_declaration
    # Add :sort: option to doxygenclass (Breathe supports it internally
    # in SectionDefTypeSubRenderer but doesn't expose it on the directive)
    DoxygenClassDirective.option_spec["sort"] = directives.flag
    return {"version": "0.3", "parallel_read_safe": True}
