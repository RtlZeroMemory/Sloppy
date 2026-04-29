import { Sloppy, Results } from "sloppy";
import express from "express";

const app = Sloppy.create();
app.mapGet("/", () => Results.text("Hello"));
export default app;
