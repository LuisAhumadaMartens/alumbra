from emitters import cpp, markdown
from emitters.base import EmitError, Emitter

EMITTERS = {
    cpp.CppEmitter.name: cpp.CppEmitter,
    markdown.MarkdownEmitter.name: markdown.MarkdownEmitter,
}

__all__ = ["EMITTERS", "EmitError", "Emitter"]
