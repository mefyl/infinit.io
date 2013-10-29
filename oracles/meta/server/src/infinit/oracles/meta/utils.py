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
  def wrapper(self, *a, **ka):
    if 'admin_token' in ka and ka['admin_token'] == ADMIN_TOKEN:
      return method(self, *a, **ka)
    self.not_found()
  return wrapper

def hash_pasword(password):
  import hashlib
  seasoned = password + conf.SALT
  seasoned = seasoned.encode('utf-8')
  return hashlib.md5(seasoned).hexdigest()

# There is probably a better way.
def stringify_object_ids(obj):
  if isinstance(obj, (bson.ObjectId, uuid.UUID)):
    return str(obj)
  if hasattr(obj, '__iter__'):
    if isinstance(obj, list):
      return [stringify_object_ids(sub) for sub in obj]
    elif isinstance(obj, pymongo.cursor.Cursor):
      return [stringify_object_ids(sub) for sub in obj]
    elif isinstance(obj, dict):
      return {key: stringify_object_ids(obj[key]) for key in obj.keys()}
    elif isinstance(obj, set):
      return (stringify_object_ids(sub) for sub in obj)
    elif isinstance(obj, str):
      return obj
    else:
      raise TypeError('unsported type %s' % type(obj))
  return obj
