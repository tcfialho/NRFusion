    for (int i = 1; i < argc; ++i) {
        if (argv[i] == nullptr)
            continue;
        const std::string argument = argv[i];
        if (argument == "--fixed-scene")
            g_fixedScene = true;
        else if (argument == "--deterministic-motion")
            g_deterministicMotion = true;
        else if (argument == "--debug-layer")
            g_debugLayer = true;
        else if (argument == "--typeless-guides")
            g_typelessGuides = true;
        else if (argument == "--nr-lifecycle")
            g_nrLifecycle = true;
        else if (argument == "--require-nr")
            g_requireNr = true;
        else if (argument == "--require-nrfusion-proxy")
            g_requireNrfusionProxy = true;
        else if (argument == "--open-menu")
            g_openMenu = true;
        else if (argument == "--require-menu") {
            g_requireMenu = true;
            g_openMenu = true;
            g_requireNrfusionProxy = true;
        }
        else if (argument == "--mfg" && i + 1 < argc) {
            g_mfgMultiplier = static_cast<std::uint32_t>(ParseCount(argv[++i]));
            if (g_mfgMultiplier < 2 || g_mfgMultiplier > 6)
                throw std::runtime_error("--mfg must be between 2 and 6");
        }
        else if (argument == "--require-mfg")
            g_requireMfg = true;
        else if (argument == "--nr-kernels" && i + 1 < argc) {
            g_kernelCsv = argv[++i];
            g_requireNr = true;
        }
        else if (argument == "--reference-on")
            g_view = 2;
        else if (argument == "--frames" && i + 1 < argc) {
            g_frameLimit = ParseCount(argv[++i]);
            if (!g_frameLimit) throw std::runtime_error("--frames must be positive");
        }
        else if (argument == "--width" && i + 1 < argc)
            g_width = ParseCount(argv[++i]);
        else if (argument == "--height" && i + 1 < argc)
            g_height = ParseCount(argv[++i]);
        else if (argument == "--warmup" && i + 1 < argc)
            g_warmup = ParseCount(argv[++i]);
        else if (argument == "--csv" && i + 1 < argc)
            g_csv = argv[++i];
        else if (argument == "--capture" && i + 1 < argc)
            g_capture = argv[++i];
        else { std::cerr << "Invalid argument: " << argument << std::endl; return 2; }
    }

    if (g_nrLifecycle && (g_typelessGuides || g_frameLimit < 180))
        throw std::runtime_error("Lifecycle requires >=180 frames and typed guides");
    if (g_fixedScene && g_deterministicMotion)
        throw std::runtime_error("Choose fixed scene or deterministic motion");
    if (g_requireMfg && g_mfgMultiplier < 2)
        throw std::runtime_error("--require-mfg requires --mfg <2..6>");
    if ((g_width != 1280 || g_height != 720) && (g_width != 1920 || g_height != 1080) &&
        (g_width != 2560 || g_height != 1440) && (g_width != 3840 || g_height != 2160)) {
        std::cerr << "Supported output sizes: 1280x720, 1920x1080, 2560x1440, 3840x2160" << std::endl;
        return 2;
    }
    if ((!g_csv.empty() || !g_capture.empty()) && (!g_frameLimit || g_frameLimit <= g_warmup)) {
        std::cerr << "Capture/CSV requires --frames greater than --warmup" << std::endl;
        return 2;
    }
    std::ofstream timings;
    if (!g_csv.empty()) {
        timings.open(g_csv);
        if (!timings) { std::cerr << "Cannot open CSV" << std::endl; return 2; }
        timings << "frame,input_width,input_height,output_width,output_height,ngx_evaluation_gpu_ms,frame_wall_ms,nr_gpu_ms,nr_passes,nr_successes,nvapi_kernels,nvapi_chains,guide_copies,guide_copy_bytes\n";
    }
