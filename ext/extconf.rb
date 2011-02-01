require 'mkmf'
require 'fileutils'

CWD = File.expand_path(File.dirname(__FILE__))

def sys(cmd)
  puts "  -- #{cmd}"
  unless ret = xsystem(cmd)
    raise "#{cmd} failed, please report to memprof@tmm1.net with pastie.org link to #{CWD}/mkmf.log"
  end
  ret
end

###
# yajl

yajl = File.basename('yajl-1.0.9.tar.gz')
dir = File.basename(yajl, '.tar.gz')

unless File.exists?("#{CWD}/dst/lib/libyajl_ext.a")
  puts "(I'm about to compile yajl.. this will definitely take a while)"

  Dir.chdir('src') do
    FileUtils.rm_rf(dir) if File.exists?(dir)

    sys("tar zxvf #{yajl}")
    Dir.chdir("#{dir}/src") do
      sys("sed -i -e 's,yajl,json,g' *.h *.c api/*.h")
      Dir['{,api/}yajl*.{h,c}'].each do |file|
        FileUtils.mv file, file.gsub('yajl', 'json')
      end

      FileUtils.mkdir_p "api/json"
      %w[ common parse gen ].each do |f|
        FileUtils.cp "api/json_#{f}.h", 'api/json/'
      end

      File.open("extconf.rb",'w') do |f|
        f.puts "require 'mkmf'; $INCFLAGS[0,0] = '-I./api/ '; create_makefile 'libyajl'"
      end

      sys("#{Config::CONFIG['bindir']}/#{Config::CONFIG['ruby_install_name']} extconf.rb")
      sys("make")

      if RUBY_PLATFORM =~ /darwin/
        sys("libtool -static -o libyajl_ext.a #{Dir['*.o'].join(' ')}")
      else
        sys("ar rv libyajl_ext.a #{Dir['*.o'].join(' ')}")
      end

      FileUtils.mkdir_p "#{CWD}/dst/lib"
      FileUtils.cp 'libyajl_ext.a', "#{CWD}/dst/lib"
      FileUtils.mkdir_p "#{CWD}/dst/include"
      FileUtils.cp_r 'api/json', "#{CWD}/dst/include/"
    end
  end
end

$LIBPATH.unshift "#{CWD}/dst/lib"
$INCFLAGS[0,0] = "-I#{CWD}/dst/include "

unless have_library('yajl_ext') and have_header('json/json_gen.h')
  raise 'Yajl build failed'
end

def add_define(name)
  $defs.push("-D#{name}")
end

if RUBY_VERSION >= "1.9"
  add_define "_RUBY_19_"
end

if RUBY_PLATFORM =~ /linux/
  ###
  # libelf

  libelf = File.basename('libelf-0.8.13.tar.gz')
  dir = File.basename(libelf, '.tar.gz')

  unless File.exists?("#{CWD}/dst/lib/libelf_ext.a")
    puts "(I'm about to compile libelf.. this will definitely take a while)"

    Dir.chdir('src') do
      FileUtils.rm_rf(dir) if File.exists?(dir)

      sys("tar zxvf #{libelf}")
      Dir.chdir(dir) do
        ENV['CFLAGS'] = '-fPIC'
        sys("./configure --prefix=#{CWD}/dst --disable-nls --disable-shared")
        sys("make")
        sys("make install")
      end
    end

    Dir.chdir('dst/lib') do
      FileUtils.ln_s 'libelf.a', 'libelf_ext.a'
    end
  end

  $LIBPATH.unshift "#{CWD}/dst/lib"
  $INCFLAGS[0,0] = "-I#{CWD}/dst/include "

  unless have_library('elf_ext', 'gelf_getshdr')
    raise 'libelf build failed'
  end

  ###
  # libdwarf

  libdwarf = File.basename('libdwarf-20091118.tar.gz')
  dir = File.basename(libdwarf, '.tar.gz').sub('lib','')

  unless File.exists?("#{CWD}/dst/lib/libdwarf_ext.a")
    puts "(I'm about to compile libdwarf.. this will definitely take a while)"

    Dir.chdir('src') do
      FileUtils.rm_rf(dir) if File.exists?(dir)

      sys("tar zxvf #{libdwarf}")
      Dir.chdir("#{dir}/libdwarf") do
        ENV['CFLAGS'] = "-fPIC -I#{CWD}/dst/include"
        ENV['LDFLAGS'] = "-L#{CWD}/dst/lib"
        sys("./configure")
        sys("make")

        FileUtils.cp 'libdwarf.a', "#{CWD}/dst/lib/libdwarf_ext.a"
        FileUtils.cp 'dwarf.h', "#{CWD}/dst/include/"
        FileUtils.cp 'libdwarf.h', "#{CWD}/dst/include/"
      end
    end
  end

  unless have_library('dwarf_ext')
    raise 'libdwarf build failed'
  end

  is_elf = true
  add_define 'HAVE_ELF'
  add_define 'HAVE_DWARF'
end

if have_header('mach-o/dyld.h')
  is_macho = true
  add_define 'HAVE_MACH'
  # XXX How to determine this properly? RUBY_PLATFORM reports "i686-darwin10.2.0" on Snow Leopard.
  add_define "_ARCH_x86_64_"

  if RUBY_VERSION < "1.9"
    sizes_of = [
      'RVALUE',
      'struct heaps_slot'
    ]

    offsets_of = {
      'struct heaps_slot' => %w[ slot limit ],
      'struct BLOCK' => %w[ body var cref self klass wrapper block_obj orig_thread dyna_vars scope prev ],
      'struct METHOD' => %w[ klass rklass recv id oid body ]
    }

    addresses_of = [
      # 'add_freelist',
      # 'rb_newobj',
      # 'freelist',
      # 'heaps',
      # 'heaps_used',
      # 'finalizer_table'
    ]

    expressions = []

    sizes_of.each do |type|
      name = type.sub(/^struct\s*/,'')
      expressions << ["sizeof__#{name}", "sizeof(#{type})"]
    end
    offsets_of.each do |type, members|
      name = type.sub(/^struct\s*/,'')
      members.each do |member|
        expressions << ["offset__#{name}__#{member}", "(size_t)&(((#{type} *)0)->#{member})"]
      end
    end
    addresses_of.each do |name|
      expressions << ["address__#{name}", "&#{name}"]
    end

    pid = fork{sleep while true}
    output = IO.popen('gdb --interpreter=mi --quiet', 'w+') do |io|
      io.puts "attach #{pid}"
      expressions.each do |name, expr|
        io.puts "-data-evaluate-expression #{expr.dump}"
      end
      io.puts 'quit'
      io.puts 'y'
      io.close_write
      io.read
    end
    Process.kill 9, pid

    attach, *results = output.grep(/^\^/).map{ |l| l.strip }
    if results.find{ |l| l =~ /^\^error/ }
      raise "Unsupported platform: #{results.inspect}"
    end

    values = results.map{ |l| l[/value="(.+?)"/, 1] }
    vars = Hash[ *expressions.map{|n,e| n }.zip(values).flatten ].each do |name, val|
      add_define "#{name}=#{val.split.first}"
    end
  end
end

arch = RUBY_PLATFORM[/(.*)-linux/,1]
case arch
when "universal"
  arch = 'x86_64'
when 'i486'
  arch = 'i386'
end
add_define "_ARCH_#{arch}_"

if ENV['MEMPROF_DEBUG'] == '1'
  add_define "_MEMPROF_DEBUG"
  $preload = ["\nCFLAGS = -Wall -Wextra -fPIC -ggdb3 -O0"]
end

if is_elf or is_macho
  $objs = Dir['{.,*}/*.c'].map{ |file| file.gsub(/\.c(pp)?$/, '.o') }
  create_makefile('memprof')

  makefile = File.read('Makefile')
  makefile.gsub!('-c $<', '-o $(patsubst %.c,%.o,$<) -c $<')
  makefile.gsub!(/(CLEANOBJS\s*=)/, '\1 */*.o')

  File.open('Makefile', 'w+'){ |f| f.puts(makefile) }
else
  raise 'unsupported platform'
end
