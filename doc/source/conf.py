import sys, os

sys.path.insert(0, os.path.abspath('extensions'))

extensions = ['sphinx.ext.autodoc', 'sphinx.ext.doctest', 'sphinx.ext.todo',
                    'sphinx.ext.coverage', 'sphinx.ext.pngmath', 'sphinx.ext.ifconfig',
                                  'sphinx.ext.autodoc']

todo_include_todos = True
templates_path = ['_templates']
source_suffix = '.rst'
master_doc = 'nr-module'
exclude_patterns = []
add_function_parentheses = True
#add_module_names = True
#modindex_common_prefix = []

project = u'NR Module'
copyright = u'2018'

version = '1.0'
release = '1.0'
