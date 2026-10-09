float ysp_main(vec2 p) {
    float a = sin(p.x * ysp_param(0) + ysp_param(2));
    float b = sin(p.y * ysp_param(1));
    return 0.5 * (a + b);
}
