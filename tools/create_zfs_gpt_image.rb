#!/usr/bin/env ruby
# frozen_string_literal: true

require "optparse"
require "securerandom"
require "zlib"

SECTOR_SIZE = 512
GPT_ENTRY_COUNT = 128
GPT_ENTRY_SIZE = 128
GPT_ENTRY_SECTORS = (GPT_ENTRY_COUNT * GPT_ENTRY_SIZE) / SECTOR_SIZE
ZFS_GUID = "6A898CC3-1DD2-11B2-99A6-080020736631"

options = {
  label: "PantheraZFS",
  type: ZFS_GUID
}

parser = OptionParser.new do |opts|
  opts.banner = "Usage: #{File.basename($PROGRAM_NAME)} --image PATH --size SIZE [--label NAME]"
  opts.on("--image PATH", "Raw disk image to create") { |value| options[:image] = value }
  opts.on("--size SIZE", "Image size, e.g. 256m, 2g") { |value| options[:size] = value }
  opts.on("--label NAME", "GPT partition label") { |value| options[:label] = value }
  opts.on("--type UUID", "GPT partition type UUID") { |value| options[:type] = value }
end
parser.parse!

def parse_size(value)
  match = /\A([0-9]+)([kKmMgG]?)\z/.match(value.to_s)
  raise ArgumentError, "invalid size: #{value.inspect}" unless match

  amount = match[1].to_i
  scale = case match[2].downcase
          when "k" then 1024
          when "m" then 1024 * 1024
          when "g" then 1024 * 1024 * 1024
          else 1
          end
  amount * scale
end

def uuid_to_gpt_bytes(uuid)
  fields = /\A([0-9a-fA-F]{8})-([0-9a-fA-F]{4})-([0-9a-fA-F]{4})-([0-9a-fA-F]{4})-([0-9a-fA-F]{12})\z/.match(uuid)
  raise ArgumentError, "invalid UUID: #{uuid.inspect}" unless fields

  [
    fields[1].to_i(16),
    fields[2].to_i(16),
    fields[3].to_i(16)
  ].pack("Vvv") + [fields[4] + fields[5]].pack("H*")
end

def utf16le_label(label)
  encoded = label.encode("UTF-16LE").byteslice(0, 72).b
  encoded + ("\0".b * (72 - encoded.bytesize))
end

image = options.fetch(:image)
size_bytes = parse_size(options.fetch(:size))
raise ArgumentError, "image size must be sector aligned" unless (size_bytes % SECTOR_SIZE).zero?

total_sectors = size_bytes / SECTOR_SIZE
minimum_sectors = 1 + 1 + GPT_ENTRY_SECTORS + 8 + GPT_ENTRY_SECTORS + 1
raise ArgumentError, "image is too small for GPT layout" if total_sectors <= minimum_sectors

last_lba = total_sectors - 1
first_usable = 34
last_usable = last_lba - GPT_ENTRY_SECTORS - 1
partition_first = 40
partition_last = last_lba - 40
raise ArgumentError, "image is too small for the ZFS partition" if partition_last <= partition_first
raise ArgumentError, "partition exceeds GPT usable range" if partition_last > last_usable

disk_guid = uuid_to_gpt_bytes(SecureRandom.uuid)
partition_guid = uuid_to_gpt_bytes(SecureRandom.uuid)
type_guid = uuid_to_gpt_bytes(options.fetch(:type))

entries = String.new(capacity: GPT_ENTRY_COUNT * GPT_ENTRY_SIZE, encoding: Encoding::BINARY)
entries << type_guid
entries << partition_guid
entries << [partition_first, partition_last, 0].pack("Q<Q<Q<")
entries << utf16le_label(options.fetch(:label))
entries << ("\0" * (GPT_ENTRY_SIZE - entries.bytesize))
entries << ("\0" * (GPT_ENTRY_COUNT * GPT_ENTRY_SIZE - entries.bytesize))
entries_crc = Zlib.crc32(entries)

def gpt_header(current_lba:, backup_lba:, first_usable:, last_usable:, disk_guid:, entries_lba:, entries_crc:)
  header = String.new(capacity: SECTOR_SIZE, encoding: Encoding::BINARY)
  header << "EFI PART"
  header << [0x00010000, 92, 0, 0].pack("V4")
  header << [current_lba, backup_lba, first_usable, last_usable].pack("Q<Q<Q<Q<")
  header << disk_guid
  header << [entries_lba, GPT_ENTRY_COUNT, GPT_ENTRY_SIZE, entries_crc].pack("Q<VVV")
  header << ("\0" * (SECTOR_SIZE - header.bytesize))

  crc_region = header.byteslice(0, 92).dup
  crc_region[16, 4] = [0].pack("V")
  header[16, 4] = [Zlib.crc32(crc_region)].pack("V")
  header
end

protective_mbr = "\0".b * SECTOR_SIZE
protective_mbr[446, 16] = [
  0x00,
  0xfe, 0xff, 0xff,
  0xee,
  0xfe, 0xff, 0xff,
  1,
  [total_sectors - 1, 0xffff_ffff].min
].pack("C4CC2VV")
protective_mbr[510, 2] = [0xaa55].pack("v")

primary_header = gpt_header(
  current_lba: 1,
  backup_lba: last_lba,
  first_usable: first_usable,
  last_usable: last_usable,
  disk_guid: disk_guid,
  entries_lba: 2,
  entries_crc: entries_crc
)
secondary_header = gpt_header(
  current_lba: last_lba,
  backup_lba: 1,
  first_usable: first_usable,
  last_usable: last_usable,
  disk_guid: disk_guid,
  entries_lba: last_lba - GPT_ENTRY_SECTORS,
  entries_crc: entries_crc
)

File.open(image, File::RDWR | File::CREAT | File::TRUNC, 0o644) do |file|
  file.truncate(size_bytes)
  file.seek(0)
  file.write(protective_mbr)
  file.seek(SECTOR_SIZE)
  file.write(primary_header)
  file.seek(2 * SECTOR_SIZE)
  file.write(entries)
  file.seek((last_lba - GPT_ENTRY_SECTORS) * SECTOR_SIZE)
  file.write(entries)
  file.seek(last_lba * SECTOR_SIZE)
  file.write(secondary_header)
end

puts "Created ZFS GPT image: #{image}"
puts "Size bytes: #{size_bytes}"
puts "Partition: #{partition_first}..#{partition_last} type #{options.fetch(:type)}"
