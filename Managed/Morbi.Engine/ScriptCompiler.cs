#nullable enable
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Emit;

namespace Morbi.Engine;

public readonly record struct ScriptCompileOutcome(
    bool Success,
    int FileCount,
    long ElapsedMs,
    string Diagnostics);

/// <summary>
/// Embedded in-process Roslyn C# compiler (Apache-2.0) that builds raw project Scripts/*.cs
/// directly into GameScripts.dll without requiring MSVC or an external .NET SDK installation.
/// </summary>
public static class ScriptCompiler
{
    private static readonly CSharpParseOptions s_parseOptions =
        CSharpParseOptions.Default.WithLanguageVersion(LanguageVersion.Latest);

    private static List<MetadataReference>? s_cachedRefs;

    public static ScriptCompileOutcome CompileDirectory(string scriptsDir, string outputDllPath, bool releaseOptimize = false)
    {
        var sw = Stopwatch.StartNew();

        if (string.IsNullOrWhiteSpace(scriptsDir) || !Directory.Exists(scriptsDir))
        {
            return new ScriptCompileOutcome(true, 0, sw.ElapsedMilliseconds, string.Empty);
        }

        string[] sourceFiles = Directory.GetFiles(scriptsDir, "*.cs", SearchOption.AllDirectories)
            .OrderBy(p => p, StringComparer.OrdinalIgnoreCase)
            .ToArray();

        if (sourceFiles.Length == 0)
        {
            return new ScriptCompileOutcome(true, 0, sw.ElapsedMilliseconds, string.Empty);
        }

        var syntaxTrees = new List<SyntaxTree>(sourceFiles.Length + 1);
        const string globalUsingsSource =
            "global using System;\n" +
            "global using System.Collections.Generic;\n" +
            "global using System.IO;\n" +
            "global using System.Linq;\n" +
            "global using System.Net.Http;\n" +
            "global using System.Threading;\n" +
            "global using System.Threading.Tasks;\n" +
            "global using Morbi.Engine;\n";
        syntaxTrees.Add(CSharpSyntaxTree.ParseText(globalUsingsSource, s_parseOptions, path: "__MorbiGlobalUsings.g.cs", encoding: Encoding.UTF8));

        foreach (string file in sourceFiles)
        {
            string text = File.ReadAllText(file, Encoding.UTF8);
            syntaxTrees.Add(CSharpSyntaxTree.ParseText(text, s_parseOptions, path: file, encoding: Encoding.UTF8));
        }

        List<MetadataReference> references = GetMetadataReferences();
        string assemblyName = Path.GetFileNameWithoutExtension(outputDllPath);
        if (string.IsNullOrWhiteSpace(assemblyName))
        {
            assemblyName = "GameScripts";
        }

        var options = new CSharpCompilationOptions(
            OutputKind.DynamicallyLinkedLibrary,
            optimizationLevel: releaseOptimize ? OptimizationLevel.Release : OptimizationLevel.Debug,
            allowUnsafe: true,
            nullableContextOptions: NullableContextOptions.Enable);

        CSharpCompilation compilation = CSharpCompilation.Create(
            assemblyName,
            syntaxTrees,
            references,
            options);

        string? outDir = Path.GetDirectoryName(outputDllPath);
        if (!string.IsNullOrEmpty(outDir))
        {
            Directory.CreateDirectory(outDir);
        }

        string pdbPath = Path.ChangeExtension(outputDllPath, ".pdb");
        string tempDll = outputDllPath + ".tmp";
        string tempPdb = pdbPath + ".tmp";

        EmitResult emitResult;
        using (var dllStream = new FileStream(tempDll, FileMode.Create, FileAccess.Write, FileShare.None))
        using (var pdbStream = new FileStream(tempPdb, FileMode.Create, FileAccess.Write, FileShare.None))
        {
            emitResult = compilation.Emit(
                peStream: dllStream,
                pdbStream: pdbStream,
                options: new EmitOptions(debugInformationFormat: DebugInformationFormat.PortablePdb));
        }

        var diagBuilder = new StringBuilder();
        foreach (Diagnostic d in emitResult.Diagnostics)
        {
            if (d.Severity == DiagnosticSeverity.Error)
            {
                diagBuilder.AppendLine(d.ToString());
            }
        }

        sw.Stop();
        if (!emitResult.Success)
        {
            TryDeleteFile(tempDll);
            TryDeleteFile(tempPdb);
            return new ScriptCompileOutcome(false, sourceFiles.Length, sw.ElapsedMilliseconds, diagBuilder.ToString().Trim());
        }

        File.Move(tempDll, outputDllPath, overwrite: true);
        File.Move(tempPdb, pdbPath, overwrite: true);

        return new ScriptCompileOutcome(true, sourceFiles.Length, sw.ElapsedMilliseconds, string.Empty);
    }

    private static List<MetadataReference> GetMetadataReferences()
    {
        if (s_cachedRefs != null)
        {
            return s_cachedRefs;
        }

        var refs = new List<MetadataReference>();
        var seenPaths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        void AddRefPath(string? path)
        {
            if (!string.IsNullOrWhiteSpace(path) && File.Exists(path) && seenPaths.Add(path))
            {
                refs.Add(MetadataReference.CreateFromFile(path));
            }
        }

        // Core engine SDK assembly
        AddRefPath(typeof(ManagedHost).Assembly.Location);

        // .NET CoreCLR platform assemblies (System.Runtime, System.Net.Sockets, System.Memory, etc.)
        if (AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") is string tpa)
        {
            foreach (string asmPath in tpa.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries))
            {
                string fileName = Path.GetFileName(asmPath);
                if (fileName.StartsWith("Microsoft.CodeAnalysis", StringComparison.OrdinalIgnoreCase))
                {
                    continue;
                }
                AddRefPath(asmPath);
            }
        }

        // Fallback loaded assemblies if TPA did not include everything
        foreach (Assembly asm in AppDomain.CurrentDomain.GetAssemblies())
        {
            if (!asm.IsDynamic && !string.IsNullOrEmpty(asm.Location))
            {
                string fileName = Path.GetFileName(asm.Location);
                if (!fileName.StartsWith("Microsoft.CodeAnalysis", StringComparison.OrdinalIgnoreCase))
                {
                    AddRefPath(asm.Location);
                }
            }
        }

        s_cachedRefs = refs;
        return s_cachedRefs;
    }

    private static void TryDeleteFile(string path)
    {
        try
        {
            if (File.Exists(path)) File.Delete(path);
        }
        catch
        {
            // Ignore temp cleanup failure
        }
    }
}
