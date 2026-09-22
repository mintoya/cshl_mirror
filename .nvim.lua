-- Enable 'exrc' in your Neovim config and trust this file with :trust.
local root = vim.fn.fnamemodify(debug.getinfo(1, 'S').source:sub(2), ':p:h')
local grammar = vim.fs.joinpath(root, '/tsversion')
local treesitter = require('nvim-treesitter')
local group = vim.api.nvim_create_augroup('CshlTreesitter', { clear = true })
local function register()
  require('nvim-treesitter.parsers').cshl = { install_info = { path = grammar, queries = 'queries' }, }
end
vim.api.nvim_create_autocmd('User', {
  group = group,
  pattern = 'TSUpdate',
  callback = register,
})
register()

if not treesitter.install({ 'cshl' }):wait(30000) then
  vim.notify('CSHL parser installation failed', vim.log.levels.ERROR)
  return
end

vim.treesitter.language.add('cshl',
  { path = vim.fs.joinpath(require('nvim-treesitter.config').get_install_dir('parser'), '/cshl.so'), })
vim.treesitter.query.set('cshl', 'highlights',
  table.concat(vim.fn.readfile(grammar .. '/queries/highlights.scm'), '\n'))
vim.filetype.add({ extension = { cshl = 'cshl' } })

vim.api.nvim_create_autocmd('FileType', {
  group = group,
  pattern = 'cshl',
  callback = function(event)
    vim.treesitter.start(event.buf, 'cshl')
  end,
})

for _, buf in ipairs(vim.api.nvim_list_bufs()) do
  if vim.api.nvim_buf_is_loaded(buf)
      and vim.api.nvim_buf_get_name(buf):match('%.cshl$') then
    vim.bo[buf].filetype = 'cshl'
    vim.treesitter.start(buf, 'cshl')
  end
end
