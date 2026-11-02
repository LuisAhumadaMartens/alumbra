import abc


class EmitError(ValueError):
    pass


class Emitter(abc.ABC):
    name = ""

    def __init__(self, options):
        self.options = options

    @abc.abstractmethod
    def render(self, model):
        raise NotImplementedError
