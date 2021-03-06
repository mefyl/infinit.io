import os

arch, osyst, comp = os.environ['BUILDFARM_NAME'].split('-')

def targets(action):
  yield '//gap/%s' % action
  yield '//frete/%s' % action
  yield '//station/%s' % action
  yield '//papier/%s' % action
  if osyst.startswith('linux') or osyst.startswith('osx'):
    yield '//oracles/%s' % action
  else:
    yield '//oracles/meta/client/%s' % action
    yield '//oracles/trophonius/client/%s' % action
  # XXX: Until moc fix on Windows, do no run fist-gui-qt/build target.
  if action == 'build':
    yield '//python'
