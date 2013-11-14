import bottle
import bson
import decorator
import inspect
import uuid
from . import error
from . import conf
import pymongo

ADMIN_TOKEN = "admintoken"

class api:

  functions = []

  def __init__(self, route = None, method = 'GET'):
    self.__route = route
    self.__method = method

  def __call__(self, method):
    import inspect
    spec = inspect.getfullargspec(method)
    def annotation_mapper(self, *args, **kwargs):
      for arg, annotation in spec.annotations.items():
        value = kwargs.get(arg, None)
        if arg is not None:
          try:
            kwargs[arg] = annotation(value)
          except:
            m = '%r is not a valid %s' % (value, annotation.__name__)
            bottle.abort(400, m)
      return method(self, *args, **kwargs)
    method.__route__ = self.__route
    method.__method__ = self.__method
    api.functions.append(method)
    return annotation_mapper

def require_logged_in(method):
  def wrapper(wrapped, self, *args, **kwargs):
    if self.user is None:
      self.forbiden()
    return wrapped(self, *args, **kwargs)
  return decorator.decorator(wrapper, method)

def require_admin(method):
  def wrapper(wrapped, self, *args, **kwargs):
    if 'admin_token' in kwargs and kwargs['admin_token'] == ADMIN_TOKEN:
      return wrapped(self, *args, **kwargs)
    self.not_found()
  return decorator.decorator(wrapper, method)

def hash_pasword(password):
  import hashlib
  seasoned = password + conf.SALT
  seasoned = seasoned.encode('utf-8')
  return hashlib.md5(seasoned).hexdigest()