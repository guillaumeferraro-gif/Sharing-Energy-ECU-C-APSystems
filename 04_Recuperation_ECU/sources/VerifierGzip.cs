using System;
using System.IO;
using System.IO.Compression;
using System.Security.Cryptography;

public static class AccEcuGzip33
{
    private static readonly uint[] Table = MakeCrcTable();

    private static uint[] MakeCrcTable()
    {
        uint[] table = new uint[256];
        for (int i = 0; i < table.Length; i++)
        {
            uint value = (uint)i;
            for (int bit = 0; bit < 8; bit++)
                value = (value >> 1) ^ (((value & 1) == 0) ? 0u : 0xedb88320u);
            table[i] = value;
        }
        return table;
    }

    private static long Check(Stream input, string ecuFooter)
    {
        if (input.Length < 18) throw new InvalidDataException("Gzip incomplet.");
        input.Position = input.Length - 8;
        byte[] footer = new BinaryReader(input).ReadBytes(8);
        string hex = BitConverter.ToString(footer).Replace("-", "");
        if (footer.Length != 8 || !String.Equals(hex, ecuFooter, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("La fin du gzip differe de celle lue sur l'ECU.");
        input.Position = 0;
        if (input.ReadByte() != 0x1f || input.ReadByte() != 0x8b || input.ReadByte() != 8)
            throw new InvalidDataException("En-tete gzip invalide.");
        input.Position = 0;
        uint crc = UInt32.MaxValue;
        long total = 0;
        byte[] buffer = new byte[65536];
        using (GZipStream gzip = new GZipStream(input, CompressionMode.Decompress, true))
        {
            int count;
            while ((count = gzip.Read(buffer, 0, buffer.Length)) > 0)
            {
                total += count;
                if (total > 536870912L) throw new InvalidDataException("Taille decompressee hors limite.");
                for (int i = 0; i < count; i++)
                    crc = Table[(int)((crc ^ buffer[i]) & 255u)] ^ (crc >> 8);
            }
        }
        // Windows uses little-endian integers, as does the gzip trailer.
        if ((crc ^ UInt32.MaxValue) != BitConverter.ToUInt32(footer, 0) || total != BitConverter.ToUInt32(footer, 4))
            throw new InvalidDataException("CRC32 ou longueur decompressee incorrecte.");
        return total;
    }

    private static void Compress(Stream input, Stream output)
    {
        using (GZipStream gzip = new GZipStream(output, CompressionMode.Compress, true))
            input.CopyTo(gzip);
    }

    private static long VerifyRoundTrip(Stream input, string expectedSha256, long expectedLength)
    {
        input.Position = input.Length - 8;
        string footer = BitConverter.ToString(new BinaryReader(input).ReadBytes(8)).Replace("-", "");
        long length = Check(input, footer);
        if (length != expectedLength) throw new InvalidDataException("Longueur TAR incorrecte apres compression sur le PC.");
        input.Position = 0;
        using (GZipStream gzip = new GZipStream(input, CompressionMode.Decompress, true))
        using (SHA256 algorithm = SHA256.Create())
        {
            string actual = BitConverter.ToString(algorithm.ComputeHash(gzip)).Replace("-", "");
            if (!String.Equals(actual, expectedSha256, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("SHA-256 du TAR different apres compression sur le PC.");
        }
        return length;
    }

    public static void CompressVerifiedTar(string tarPath, string gzipPath, string expectedSha256)
    {
        long length;
        using (FileStream input = File.OpenRead(tarPath))
        using (FileStream output = new FileStream(gzipPath, FileMode.CreateNew, FileAccess.Write, FileShare.None))
        {
            length = input.Length;
            Compress(input, output);
        }
        using (FileStream output = File.OpenRead(gzipPath))
            VerifyRoundTrip(output, expectedSha256, length);
    }

    public static void SelfTest()
    {
        byte[] fixture = Convert.FromBase64String("H4sIAAAAAAACAzM0MjYxNTO3sAQAJjn0ywkAAAA=");
        const string footer = "2639f4cb09000000";
        using (MemoryStream input = new MemoryStream(fixture))
            if (Check(input, footer) != 9) throw new InvalidDataException("Autotest gzip du PC en echec.");
        using (MemoryStream plain = new MemoryStream(System.Text.Encoding.ASCII.GetBytes("123456789")))
        using (MemoryStream packed = new MemoryStream())
        {
            Compress(plain, packed);
            VerifyRoundTrip(packed, "15e2b0d3c33891ebb0f1ef609ec419420c20e320ce94c65fbc8c3312448eb225", 9);
        }
        fixture[fixture.Length - 8] ^= 1;
        using (MemoryStream input = new MemoryStream(fixture))
        {
            try { Check(input, "2739f4cb09000000"); }
            catch (InvalidDataException) { return; }
            catch (IOException) { return; }
        }
        throw new InvalidDataException("Autotest CRC32 du PC en echec.");
    }

    public static long VerifyFile(string path, string ecuFooter)
    {
        using (FileStream input = File.OpenRead(path)) return Check(input, ecuFooter);
    }
}
