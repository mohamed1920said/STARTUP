using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;
using SolidWorks.Interop.swmotionstudy;

// Builds the corrected native actuator assembly and creates a real SOLIDWORKS
// Motion Study.  Strongly typed double[] transforms are used because VBScript
// SAFEARRAY values are rejected by IMathUtility on some SOLIDWORKS 2026 builds.
internal static class CreateSolidWorksMotion
{
    private const double Pi = Math.PI;

    private sealed class Item
    {
        public readonly string BaseName;
        public readonly double Xmm;
        public readonly double Ymm;
        public readonly double Zmm;
        public readonly string Orientation;
        public readonly bool Moves;
        public readonly double MotorDisplacementDeg;

        public Item(string baseName, double x, double y, double z, string orientation,
                    bool moves, double motorDisplacementDeg)
        {
            BaseName = baseName;
            Xmm = x;
            Ymm = y;
            Zmm = z;
            Orientation = orientation;
            Moves = moves;
            MotorDisplacementDeg = motorDisplacementDeg;
        }
    }

    private static readonly Item[] Items = new Item[]
    {
        new Item("01_Lower_Base_Plate", 0, 0, 0, "top", false, 0),
        new Item("02_Upper_Support_Plate", 0, 0, 81, "top", false, 0),
        new Item("03_Motor_Bracket", -45, 0, 7, "top", false, 0),
        new Item("04_Motor_Gear_40T", -45, 0, 29.5, "gear_motor", true, -180),
        new Item("05_Output_Gear_80T", 45, 0, 28, "gear_output", true, 90),
        new Item("06_Output_Shaft", 45, 0, -34, "top", true, 90),
        new Item("07_Output_Gear_Hub", 45, 0, 22, "top", true, 90),
        new Item("08_Clutch_Pressure_Plate", 45, 0, 48, "top", true, 90),
        new Item("09_Belleville_Stack_Reference", 45, 0, 53, "top", true, 90),
        new Item("10_Valve_Stem_Adapter", 45, 0, -52, "top", true, 90),
        new Item("11_Valve_Clamp_50mm", 45, 0, -66, "top", false, 0),
        new Item("13_AS5600_Bracket", 45, 0, 139, "top", false, 0),
        new Item("14_Magnet_Carrier", 45, 0, 127, "top", true, 90),
        new Item("15_Reed_Switch_Cam", 45, 0, 87, "top", true, 90),
        new Item("16_Reed_Bracket_OPEN", 88, 38, 102, "top", false, 0),
        new Item("17_Reed_Bracket_CLOSED", 88, -38, 102, "top", false, 0),
        new Item("18_Microswitch_Bracket_OPEN", 100, 58, 97, "top", false, 0),
        new Item("19_Microswitch_Bracket_CLOSED", 100, -58, 97, "top", false, 0),
        new Item("20_Mechanical_Stop_Bracket", 45, -65, 87, "top", false, 0),
        new Item("21_Battery_Tray", -75, 58, 106, "top", false, 0),
        new Item("22_TTGO_Tray", -58, -48, 106, "top", false, 0),
        new Item("23_Motor_Driver_Tray", 68, 52, 106, "top", false, 0),
        new Item("24_Manual_Override", 45, 0, 97, "top", true, 90),
        new Item("25_Gear_Enclosure_Cover", 0, 0, 170, "cover", false, 0),
        new Item("26_Lower_Bearing_Carrier", 45, 0, 6, "top", false, 0),
        new Item("27_Upper_Bearing_Carrier", 45, 0, 65, "top", false, 0),
        new Item("REF_6204_2RS_Bearing", 45, 0, 7, "top", false, 0),
        new Item("REF_6204_2RS_Bearing", 45, 0, 66, "top", false, 0),
        new Item("REF_5840_31ZY_Motor", -45, 0, -44, "top", false, 0),
        new Item("REF_AS5600_PCB", 45, 0, 144, "top", false, 0),
        new Item("REF_TTGO_LORA32", -58, -48, 111, "top", false, 0),
        new Item("REF_BTS7960_IBT2", 68, 52, 111, "top", false, 0),
        new Item("REF_18650_Cell", -107.7, 34, 121, "cell_x", false, 0),
        new Item("REF_18650_Cell", -107.7, 58, 121, "cell_x", false, 0),
        new Item("REF_18650_Cell", -107.7, 82, 121, "cell_x", false, 0)
    };

    private static SldWorks _sw;
    private static TextWriter _log;

    public static int Main(string[] args)
    {
        if (args.Length < 1 || args.Length > 2)
        {
            Console.Error.WriteLine("Usage: create_solidworks_motion.exe PROJECT_ROOT [motion-only|export-only|validate-only]");
            return 2;
        }

        string root = Path.GetFullPath(args[0]);
        string nativeDir = Path.Combine(root, "hardware", "actuator", "cad", "native");
        string motionDir = Path.Combine(root, "hardware", "actuator", "cad", "motion");
        Directory.CreateDirectory(nativeDir);
        Directory.CreateDirectory(motionDir);
        string reportPath = Path.Combine(motionDir, "AMR_Valve_Open_Close_REPORT.txt");

        using (StreamWriter report = new StreamWriter(reportPath, false))
        {
            report.AutoFlush = true;
            _log = report;
            try
            {
                Type swType = Type.GetTypeFromProgID("SldWorks.Application", true);
                _sw = (SldWorks)Activator.CreateInstance(swType);
                _sw.Visible = false;
                _sw.UserControl = false;
                Log("SOLIDWORKS " + _sw.RevisionNumber());

                string asmPath = Path.Combine(nativeDir, "AMR_50mm_Valve_Actuator.SLDASM");
                bool exportOnly = args.Length == 2 && String.Equals(args[1], "export-only", StringComparison.OrdinalIgnoreCase);
                bool validateOnly = args.Length == 2 && String.Equals(args[1], "validate-only", StringComparison.OrdinalIgnoreCase);
                if (args.Length < 2)
                    asmPath = BuildAssembly(root, nativeDir);
                else if (!File.Exists(asmPath))
                    throw new FileNotFoundException("Native assembly", asmPath);
                if (exportOnly || validateOnly)
                    ExportExistingMotionStudy(asmPath, motionDir, exportOnly);
                else
                    CreateMotionStudy(asmPath, motionDir);
                Log("RESULT=PASS");
                Console.WriteLine("Motion Study complete: " + asmPath);
                return 0;
            }
            catch (Exception ex)
            {
                Log("RESULT=FAIL");
                Log(ex.ToString());
                Console.Error.WriteLine(ex.ToString());
                return 1;
            }
            finally
            {
                if (_sw != null)
                {
                    try { _sw.ExitApp(); }
                    catch { }
                    try { Marshal.FinalReleaseComObject(_sw); }
                    catch { }
                    _sw = null;
                }
            }
        }
    }

    private static string BuildAssembly(string root, string nativeDir)
    {
        string template = @"C:\ProgramData\SOLIDWORKS\SOLIDWORKS 2026\templates\Assembly.ASMDOT";
        if (!File.Exists(template)) throw new FileNotFoundException("Assembly template", template);

        ModelDoc2 model = (ModelDoc2)_sw.NewDocument(template, 0, 0, 0);
        if (model == null) throw new InvalidOperationException("Could not create assembly document.");
        AssemblyDoc assy = (AssemblyDoc)model;
        string asmPath = Path.Combine(nativeDir, "AMR_50mm_Valve_Actuator.SLDASM");
        int firstSave = model.SaveAs3(asmPath, 0, 1);
        if (firstSave != 0) throw new InvalidOperationException("Initial assembly save failed: " + firstSave);

        MathUtility math = (MathUtility)_sw.GetMathUtility();
        int added = 0;
        for (int index = 0; index < Items.Length; index++)
        {
            Item item = Items[index];
            string partPath = Path.Combine(nativeDir, item.BaseName + ".SLDPRT");
            if (!File.Exists(partPath)) throw new FileNotFoundException("Native part", partPath);

            int openErrors = 0;
            int openWarnings = 0;
            ModelDoc2 part = _sw.OpenDoc6(partPath, 1, 1, "", ref openErrors, ref openWarnings);
            if (part == null) throw new InvalidOperationException("Could not preload " + item.BaseName + ": " + openErrors);

            int activateErrors = 0;
            _sw.ActivateDoc3(model.GetTitle(), false, 0, ref activateErrors);
            Component2 comp = assy.AddComponent5(partPath, 0, "", false, "",
                M(item.Xmm), M(item.Zmm), M(-item.Ymm));
            if (comp == null) throw new InvalidOperationException("Could not insert " + item.BaseName);

            double[] matrix = MatrixFor(item);
            MathTransform transform = (MathTransform)math.CreateTransform(matrix);
            if (transform == null) throw new InvalidOperationException("Could not create transform for " + item.BaseName);
            comp.Transform2 = transform;

            model.ClearSelection2(true);
            comp.Select4(false, null, false);
            assy.FixComponent();
            model.ClearSelection2(true);
            added++;
            Log("COMPONENT_OK=" + comp.Name2);
        }

        model.ForceRebuild3(false);
        int saveResult = model.SaveAs3(asmPath, 0, 1);
        if (saveResult != 0) throw new InvalidOperationException("Assembly save failed: " + saveResult);
        Log("ASSEMBLY_COMPONENTS=" + added);
        Log("ASSEMBLY_PATH=" + asmPath);
        return asmPath;
    }

    private static void CreateMotionStudy(string asmPath, string motionDir)
    {
        ModelDoc2 model = (ModelDoc2)_sw.ActiveDoc;
        if (model == null || !String.Equals(model.GetPathName(), asmPath, StringComparison.OrdinalIgnoreCase))
        {
            int errors = 0;
            int warnings = 0;
            model = _sw.OpenDoc6(asmPath, 2, 1, "", ref errors, ref warnings);
            if (model == null) throw new InvalidOperationException("Could not open assembly for motion: " + errors);
        }
        AssemblyDoc assy = (AssemblyDoc)model;

        MotionStudyManager manager = (MotionStudyManager)model.Extension.GetMotionStudyManager();
        if (manager == null) throw new InvalidOperationException("Motion Study Manager is unavailable.");
        manager.DeleteMotionStudy("AMR_Valve_Open_Close");
        MotionStudy study = manager.CreateMotionStudy();
        if (study == null) throw new InvalidOperationException("Could not create Motion Study.");
        study.Name = "AMR_Valve_Open_Close";
        // Basic Motion is the highest study type available on this workstation.
        // It persists motor features without requiring a Premium Motion license.
        study.StudyType = 2; // swMotionStudyTypePhysicalSimulation: Basic Motion
        int supportedTypes = 0;
        bool supportedQuery = study.GetSupportedStudyTypes(out supportedTypes);
        Log("MOTION_SUPPORTED_QUERY=" + supportedQuery);
        Log("MOTION_SUPPORTED_TYPES=" + supportedTypes);
        if (!study.SetDuration(6.0)) throw new InvalidOperationException("Could not set Motion Study duration.");
        study.PlayMode = 2; // loop
        study.Activate();

        int motors = 0;
        for (int index = 0; index < Items.Length; index++)
        {
            Item item = Items[index];
            if (!item.Moves) continue;
            Component2 comp = FindNthComponent(assy, item.BaseName, OccurrenceAt(index));
            if (comp == null) throw new InvalidOperationException("Motion component not found: " + item.BaseName);

            model.ClearSelection2(true);
            comp.Select4(false, null, false);
            assy.UnfixComponent();
            model.ClearSelection2(true);

            object motorReference = FindMotorReference(comp);
            if (motorReference == null)
            {
                Log("MOTOR_SKIPPED_NO_AXIS_REFERENCE=" + comp.Name2);
                continue;
            }

            object rawDefinition = study.CreateDefinition((int)swFeatureNameID_e.swFmAEMRotationalMotor);
            ISimulationMotorFeatureData motor = rawDefinition as ISimulationMotorFeatureData;
            if (motor == null) throw new InvalidOperationException("Could not create motor definition for " + comp.Name2);
            motor.Location = motorReference;
            motor.DirectionReference = motorReference;
            motor.MotionType = (int)swSimulationMotorMotionType_e.swSimulationMotorMotion_Harmonic;
            motor.DriveType = (int)swSimulationMotorDriveType_e.swSimulationMotorDrive_Displacement;
            motor.ReverseDirection = item.MotorDisplacementDeg < 0;
            motor.OscillatingMotor(Math.Abs(item.MotorDisplacementDeg), 1.0 / 6.0);
            Feature feature = study.CreateFeature(motor) as Feature;
            if (feature == null)
            {
                Log("MOTOR_CREATE_FAILED=" + comp.Name2);
                continue;
            }
            feature.Name = "Motor_" + item.BaseName;
            motors++;
            Log("MOTOR_OK=" + feature.Name);
        }

        if (motors < 2) throw new InvalidOperationException("Too few motor features were created: " + motors);
        model.ForceRebuild3(false);
        int saveErrors = 0;
        int saveWarnings = 0;
        bool nativeSaved = model.Save3(1, ref saveErrors, ref saveWarnings);
        Log("MOTION_NATIVE_SAVE=" + nativeSaved + " errors=" + saveErrors + " warnings=" + saveWarnings);
        if (!nativeSaved || saveErrors != 0)
            throw new InvalidOperationException("Motion assembly native save failed: " + saveErrors);

        bool calculated = study.Calculate();
        Log("MOTION_CALCULATED=" + calculated);
        Log("MOTION_NAME=" + study.Name);
        Log("MOTION_TYPE=" + study.StudyType);
        Log("MOTION_DURATION_SECONDS=" + study.GetDuration().ToString("0.###"));
        Log("MOTION_FEATURES=" + study.GetMotionFeaturesCount());
        Log("MOTION_MOTORS=" + motors);

        saveErrors = 0;
        saveWarnings = 0;
        nativeSaved = model.Save3(1, ref saveErrors, ref saveWarnings);
        Log("MOTION_FINAL_SAVE=" + nativeSaved + " errors=" + saveErrors + " warnings=" + saveWarnings);
        if (!nativeSaved || saveErrors != 0)
            throw new InvalidOperationException("Final assembly save failed: " + saveErrors);
    }

    private static void ExportExistingMotionStudy(string asmPath, string motionDir, bool captureFrames)
    {
        int errors = 0;
        int warnings = 0;
        ModelDoc2 model = _sw.OpenDoc6(asmPath, 2, 1, "", ref errors, ref warnings);
        if (model == null) throw new InvalidOperationException("Could not reopen motion assembly: " + errors);
        AssemblyDoc assy = (AssemblyDoc)model;
        MotionStudyManager manager = (MotionStudyManager)model.Extension.GetMotionStudyManager();
        MotionStudy study = manager.GetMotionStudy("AMR_Valve_Open_Close");
        if (study == null) throw new InvalidOperationException("Saved Motion Study was not found after reopen.");
        if (!study.Activate()) throw new InvalidOperationException("Saved Motion Study could not be activated.");

        int componentCount = assy.GetComponentCount(false);
        int featureCount = study.GetMotionFeaturesCount();
        double duration = study.GetDuration();
        Log("REOPEN_COMPONENTS=" + componentCount);
        Log("REOPEN_MOTION_NAME=" + study.Name);
        Log("REOPEN_MOTION_TYPE=" + study.StudyType);
        Log("REOPEN_MOTION_DURATION_SECONDS=" + duration.ToString("0.###"));
        Log("REOPEN_MOTION_FEATURES=" + featureCount);
        if (componentCount != 35 || study.StudyType != 2 || Math.Abs(duration - 6.0) > 0.001)
            throw new InvalidOperationException("Saved assembly or Motion Study validation failed.");

        bool reopenCalculated = study.Calculate();
        Log("REOPEN_CALCULATED=" + reopenCalculated);
        Log("REOPEN_MOTION_FEATURES_AFTER_CALCULATE=" + study.GetMotionFeaturesCount());
        Component2 motorGear = FindNthComponent(assy, "04_Motor_Gear_40T", 1);
        Component2 outputGear = FindNthComponent(assy, "05_Output_Gear_80T", 1);
        if (motorGear == null || outputGear == null)
            throw new InvalidOperationException("Could not find both gears during reopen validation.");
        study.SetTime(0.0);
        double[] motorAtZero = TransformData(motorGear);
        double[] outputAtZero = TransformData(outputGear);
        study.SetTime(1.5);
        double motorDelta = TransformDifference(motorAtZero, TransformData(motorGear));
        double outputDelta = TransformDifference(outputAtZero, TransformData(outputGear));
        double motorTranslationDelta = TranslationDifference(motorAtZero, TransformData(motorGear));
        double outputTranslationDelta = TranslationDifference(outputAtZero, TransformData(outputGear));
        Log("REOPEN_MOTOR_GEAR_TRANSFORM_DELTA=" + motorDelta.ToString("0.000000"));
        Log("REOPEN_OUTPUT_GEAR_TRANSFORM_DELTA=" + outputDelta.ToString("0.000000"));
        Log("REOPEN_MOTOR_GEAR_TRANSLATION_DELTA=" + motorTranslationDelta.ToString("0.000000"));
        Log("REOPEN_OUTPUT_GEAR_TRANSLATION_DELTA=" + outputTranslationDelta.ToString("0.000000"));
        if (motorDelta < 0.000001 || outputDelta < 0.000001 ||
            motorTranslationDelta > 0.00001 || outputTranslationDelta > 0.00001)
            throw new InvalidOperationException("Saved Motion Study does not rotate both gears about fixed centers.");

        Component2 savedCover = FindNthComponent(assy, "25_Gear_Enclosure_Cover", 1);
        if (savedCover == null) throw new InvalidOperationException("Saved enclosure cover is missing.");
        Log("REOPEN_COVER_VISIBLE=" + savedCover.Visible);
        Log("REOPEN_COVER_SUPPRESSION=" + savedCover.GetSuppression2());
        if (!captureFrames)
        {
            string savedFramesDir = Path.Combine(motionDir, "frames");
            int savedFrames = Directory.Exists(savedFramesDir) ? Directory.GetFiles(savedFramesDir, "frame_*.bmp").Length : 0;
            bool gifExists = File.Exists(Path.Combine(motionDir, "AMR_Valve_Open_Close.gif"));
            bool posterExists = File.Exists(Path.Combine(motionDir, "AMR_Valve_Open_Close_Poster.png"));
            Log("RAW_FRAME_COUNT=" + savedFrames);
            Log("GIF_EXISTS=" + gifExists);
            Log("POSTER_EXISTS=" + posterExists);
            if (savedCover.Visible != (int)swComponentVisibilityState_e.swComponentVisible ||
                savedCover.GetSuppression2() < (int)swComponentSuppressionState_e.swComponentFullyResolved ||
                !gifExists || !posterExists)
                throw new InvalidOperationException("Saved display state or preview artifact validation failed.");
            return;
        }

        _sw.Visible = true;
        int activateErrors = 0;
        _sw.ActivateDoc3(model.GetTitle(), true, 0, ref activateErrors);
        Component2 cover = FindNthComponent(assy, "25_Gear_Enclosure_Cover", 1);
        if (cover != null)
        {
            int coverState = cover.SetSuppression2((int)swComponentSuppressionState_e.swComponentSuppressed);
            Log("PREVIEW_COVER_SUPPRESSION_STATE=" + coverState);
            model.ClearSelection2(true);
            cover.Select4(false, null, false);
            assy.HideComponent();
            model.ClearSelection2(true);
        }
        model.ShowNamedView2("*Isometric", (int)swStandardViews_e.swIsometricView);
        model.ForceRebuild3(false);
        model.GraphicsRedraw2();
        model.ViewZoomtofit2();
        ModelView activeView = model.ActiveView as ModelView;
        if (activeView != null) activeView.EnableGraphicsUpdate = true;

        string framesDir = Path.Combine(motionDir, "frames");
        Directory.CreateDirectory(framesDir);
        string[] oldFrames = Directory.GetFiles(framesDir, "frame_*.bmp");
        for (int i = 0; i < oldFrames.Length; i++) File.Delete(oldFrames[i]);

        const int fps = 6;
        int frameCount = (int)Math.Round(duration * fps) + 1;
        int nearestTimeFrames = 0;
        for (int frame = 0; frame < frameCount; frame++)
        {
            double time = Math.Min(duration, frame / (double)fps);
            if (!study.SetTime(time)) nearestTimeFrames++;
            model.ForceRebuild3(false);
            model.GraphicsRedraw2();
            if (activeView != null) activeView.GraphicsRedraw(null);
            Thread.Sleep(75);
            string framePath = Path.Combine(framesDir, "frame_" + frame.ToString("0000") + ".bmp");
            if (!model.SaveBMP(framePath, 1280, 720))
                throw new InvalidOperationException("Could not capture frame " + frame);
        }
        study.SetTime(0.0);
        Log("FRAME_RATE=" + fps);
        Log("FRAME_COUNT=" + frameCount);
        Log("FRAMES_USING_NEAREST_SOLVER_TIME=" + nearestTimeFrames);
        Log("FRAMES_PATH=" + framesDir);
    }

    private static Component2 FindNthComponent(AssemblyDoc assy, string baseName, int occurrence)
    {
        object raw = assy.GetComponents(false);
        object[] comps = raw as object[];
        if (comps == null) return null;
        int found = 0;
        for (int i = 0; i < comps.Length; i++)
        {
            Component2 comp = comps[i] as Component2;
            if (comp == null) continue;
            string path = comp.GetPathName();
            if (String.Equals(Path.GetFileNameWithoutExtension(path), baseName, StringComparison.OrdinalIgnoreCase))
            {
                found++;
                if (found == occurrence) return comp;
            }
        }
        return null;
    }

    private static int OccurrenceAt(int itemIndex)
    {
        int count = 0;
        for (int i = 0; i <= itemIndex; i++)
            if (Items[i].BaseName == Items[itemIndex].BaseName) count++;
        return count;
    }

    private static object FindMotorReference(Component2 comp)
    {
        object bodyInfo;
        object rawBodies = comp.GetBodies3((int)swBodyType_e.swSolidBody, out bodyInfo);
        object[] bodies = rawBodies as object[];
        if (bodies == null)
        {
            Body2 singleBody = comp.GetBody() as Body2;
            if (singleBody == null) return null;
            bodies = new object[] { singleBody };
        }
        Face2 best = null;
        double bestArea = -1.0;
        for (int b = 0; b < bodies.Length; b++)
        {
            Body2 body = bodies[b] as Body2;
            if (body == null) continue;
            object[] faces = body.GetFaces() as object[];
            if (faces == null) continue;
            for (int f = 0; f < faces.Length; f++)
            {
                Face2 face = faces[f] as Face2;
                if (face == null) continue;
                Surface surface = face.GetSurface() as Surface;
                if (surface != null && surface.IsCylinder())
                {
                    double area = face.GetArea();
                    if (area > bestArea)
                    {
                        bestArea = area;
                        best = face;
                    }
                }
            }
        }
        if (best != null) return best;

        Edge bestEdge = null;
        double bestRadius = -1.0;
        for (int b = 0; b < bodies.Length; b++)
        {
            Body2 body = bodies[b] as Body2;
            if (body == null) continue;
            object[] edges = body.GetEdges() as object[];
            if (edges == null) continue;
            for (int e = 0; e < edges.Length; e++)
            {
                Edge edge = edges[e] as Edge;
                if (edge == null) continue;
                Curve curve = edge.GetCurve() as Curve;
                if (curve == null || !curve.IsCircle()) continue;
                double radius = 0.0;
                double[] circle = curve.CircleParams as double[];
                if (circle != null && circle.Length > 6) radius = Math.Abs(circle[6]);
                if (radius >= bestRadius)
                {
                    bestRadius = radius;
                    bestEdge = edge;
                }
            }
        }
        if (bestEdge != null) return bestEdge;

        Face2 bestPlane = null;
        bestArea = -1.0;
        for (int b = 0; b < bodies.Length; b++)
        {
            Body2 body = bodies[b] as Body2;
            if (body == null) continue;
            object[] faces = body.GetFaces() as object[];
            if (faces == null) continue;
            for (int f = 0; f < faces.Length; f++)
            {
                Face2 face = faces[f] as Face2;
                if (face == null) continue;
                Surface surface = face.GetSurface() as Surface;
                if (surface != null && surface.IsPlane())
                {
                    double area = face.GetArea();
                    if (area > bestArea)
                    {
                        bestArea = area;
                        bestPlane = face;
                    }
                }
            }
        }
        return bestPlane;
    }

    private static double[] MatrixFor(Item item)
    {
        double tx = M(item.Xmm);
        double ty = M(item.Zmm);
        double tz = M(-item.Ymm);
        if (item.Orientation == "top")
            return new double[] { 1, 0, 0, 0, 1, 0, 0, 0, 1, tx, ty, tz, 1, 0, 0, 0 };
        if (item.Orientation == "cover")
            return new double[] { 1, 0, 0, 0, -1, 0, 0, 0, -1, tx, ty, tz, 1, 0, 0, 0 };
        if (item.Orientation == "cell_x")
            return new double[] { 0, 1, 0, -1, 0, 0, 0, 0, 1, tx, ty, tz, 1, 0, 0, 0 };
        if (item.Orientation == "gear_motor")
            return new double[] { 1, 0, 0, 0, 0, 1, 0, -1, 0, tx, ty, tz, 1, 0, 0, 0 };
        if (item.Orientation == "gear_output")
        {
            double a = Math.Cos(2.25 * Pi / 180.0);
            double s = Math.Sin(2.25 * Pi / 180.0);
            return new double[] { a, -s, 0, 0, 0, 1, -s, -a, 0, tx, ty, tz, 1, 0, 0, 0 };
        }
        throw new InvalidOperationException("Unknown orientation: " + item.Orientation);
    }

    private static double[] TransformData(Component2 comp)
    {
        MathTransform transform = comp.Transform2;
        if (transform == null) return null;
        return transform.ArrayData as double[];
    }

    private static double TransformDifference(double[] first, double[] second)
    {
        if (first == null || second == null || first.Length < 12 || second.Length < 12)
            return 0.0;
        double sum = 0.0;
        for (int i = 0; i < 12; i++) sum += Math.Abs(first[i] - second[i]);
        return sum;
    }

    private static double TranslationDifference(double[] first, double[] second)
    {
        if (first == null || second == null || first.Length < 12 || second.Length < 12)
            return Double.MaxValue;
        return Math.Abs(first[9] - second[9]) + Math.Abs(first[10] - second[10]) +
               Math.Abs(first[11] - second[11]);
    }

    private static double M(double mm) { return mm / 1000.0; }

    private static void Log(string message)
    {
        string line = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + " " + message;
        _log.WriteLine(line);
        Console.WriteLine(line);
    }
}
